// 2009 lab616.com, All Rights Reserved.
package com.lab616.ib.api;

import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.log4j.Logger;

import com.google.common.base.Function;
import com.google.common.base.Predicate;
import com.google.inject.Inject;
import com.google.inject.internal.Lists;
import com.google.inject.internal.Maps;
import com.google.inject.name.Named;
import com.lab616.common.Pair;
import com.lab616.concurrent.QueueProcessor;
import com.lab616.ib.api.TWSConnectionProfileManager.HostPort;
import com.lab616.monitoring.Varz;
import com.lab616.monitoring.Varzs;
import com.lab616.omnibus.Kernel.Shutdown;
import com.lab616.omnibus.event.EventEngine.Stoppable;
import com.lab616.omnibus.http.servlets.StatusServlet;
import java.util.concurrent.Future;
import java.util.concurrent.FutureTask;

/**
 * A SystemEvent listener that can control the Api clients.  This provides
 * an EventEngine interface to the IB API clients.
 *
 * @author david
 *
 */
public final class TWSClientManager {

  @Varz(name = "tws-started-clients")
  public static AtomicInteger managedClients = new AtomicInteger(0);

  static {
    Varzs.export(StatusServlet.class);
  }
  static Logger logger = Logger.getLogger(TWSClientManager.class);

  /**
   * Objects to be managed by the service. 
   */
  public interface Managed extends Stoppable {
    public boolean isReady(long... timeout);
  }
  
  private final TWSClient.Factory factory;
  private final ExecutorService executor;
  private final TWSConnectionProfileManager profiles;
  private final AtomicInteger roundRobbin;
  private final Map<String, Pair<ClientWorkQueue, TWSClient>> clients;
  private final Map<TWSClient, List<Managed>> managed;

  // Tracks profile and number of clients for that profile.
  private final Map<String, Integer> profileInstanceCount;

  @Inject
  public TWSClientManager(TWSClient.Factory factory,
    @Named("tws-executor") ExecutorService executor,
    TWSConnectionProfileManager profiles) {
    this.factory = factory;
    this.executor = executor;
    this.profiles = profiles;
    this.clients = Maps.newHashMap();
    this.managed = Maps.newHashMap();
    this.profileInstanceCount = Maps.newHashMap();
    this.roundRobbin = new AtomicInteger(0);
  }

  public void addProfile(String name, String host, Integer port) {
    this.profiles.addProfile(name, new HostPort(host, port));
  }

  public boolean profileExists(String name) {
    return this.profiles.exists(name);
  }

  /**
   * Queue for work that depends on the client being properly connected.
   */
  class ClientWorkQueue extends QueueProcessor<Function<TWSClient, ?>, Void> {

    TWSClient client;

    ClientWorkQueue(String name, int id) {
      super(makeKey(name, id), false);
    }

    public void set(TWSClient c) {
      client = c;
    }

    @Override
    protected boolean take() {
      // Do not take until the client is not null and is ready.
      return client != null && client.isReady();
    }

    @Override
    protected void execute(Function<TWSClient, ?> work) throws Exception {
      Object result = work.apply(client);
      // Managed objects. See proto/csv writer.  If the result is a 
      // managed object then associate the result with this client.
      if (result instanceof Managed) {
        if (managed.get(client) == null) {
          List<Managed> list = Lists.newArrayList();
          managed.put(client, list);
        }
        managed.get(client).add((Managed) result);
      }
    }
  }

  private static String makeKey(String name, int id) {
    return String.format("%s-%d", name, id);
  }

  private Pair<ClientWorkQueue, TWSClient> getManagedClient(
    String name, int id) {
    return clients.get(makeKey(name, id));
  }

  private Pair<ClientWorkQueue, TWSClient> getManagedClient(int i) {
    synchronized (clients) {
      List<Pair<ClientWorkQueue, TWSClient>> from = Lists.newArrayList(
        clients.values());
      return from.get(i);
    }
  }

  private void addManagedClient(String name, int id,
    Pair<ClientWorkQueue, TWSClient> pair) {
    synchronized (clients) {
      managedClients.incrementAndGet();
      clients.put(makeKey(name, id), pair);
    }
  }

  public List<Pair<String, TWSClient.State>> getStats() {
    List<Pair<String, TWSClient.State>> results = Lists.newArrayList();
    for (Pair<ClientWorkQueue, TWSClient> p : clients.values()) {
      results.add(Pair.of(p.second.getId(), p.second.getState()));
    }
    return results;
  }

  private Pair<ClientWorkQueue, TWSClient> removeManagedClient(
    String name, int id) {
    synchronized (clients) {
      managedClients.decrementAndGet();
      return clients.remove(makeKey(name, id));
    }
  }

  /**
   * Searches through a list of managed components associated with a client,
   * for example, a CSV writer, and return the first component that matches
   * the provided filter.
   * @param name Name of the client.
   * @param cond The filter condition.
   * @return The first match.
   */
  public Managed findAssociatedComponent(String name, int id,
    Predicate<Managed> cond) {
    Pair<ClientWorkQueue, TWSClient> p = getManagedClient(name, id);
    if (p != null && managed.get(p.second) != null) {
      for (Managed comp : managed.get(p.second)) {
        if (cond.apply(comp)) {
          return comp;
        }
      }
    }
    return null;
  }

  public int getConnectionCount() {
  	logger.info(">>> clients = " + clients);
    return clients.size();
  }

  public static class ConnectionStatus {
  	private boolean connected;
  	private int clientId;
  	private Future<Boolean> connectedFuture;
  	
  	ConnectionStatus(int clientId, boolean connected) {
  		this.clientId = clientId;
  		this.connected = connected;
  	}
  	
  	ConnectionStatus(int clientId, Future<Boolean> connected) {
  		this.clientId = clientId;
  		this.connectedFuture = connected;
  	}

  	public Integer getClientId() {
  		return clientId;
  	}
  	
  	public boolean isConnected() {
  		try {
    		return isConnected(5, TimeUnit.SECONDS);
  		} catch (Exception e) {
  			return false;
  		}
  	}
  	
  	public boolean isConnected(long timeout, TimeUnit unit) 
  		throws ExecutionException, TimeoutException, InterruptedException {
  		if (connectedFuture == null) {
  			return connected;
  		}
  		return connectedFuture.get(timeout, unit);
  	}
  }
  /**
   * Starts a new connection of given name and assigns a connection id.
   * @param profile The name of the connection.
   * @return True if this is a blocking call.
   */
  public synchronized ConnectionStatus newConnection(final String profile,
    boolean... simulate) {
    final int id;
    if (this.profileInstanceCount.containsKey(profile)) {
      id = this.profileInstanceCount.get(profile);
    } else {
      id = 0;
      this.profileInstanceCount.put(profile, id + 1);
    }
    // Get the work queue for this client:
    Pair<ClientWorkQueue, TWSClient> p = getManagedClient(profile, id);
    if (p != null) {
      return new ConnectionStatus(id, p.second.isReady());
    }

    // The queue processor thread for this client.
    final ClientWorkQueue queue = new ClientWorkQueue(profile, id);

    boolean sim = (simulate.length > 0) ? simulate[0] : false;
    final TWSClient client = this.factory.create(profile, id, sim);

    // Register this pair.
    addManagedClient(profile, id, Pair.of(queue, client));
    logger.info("Created work queue and client for " + makeKey(profile, id));


    // Start the queue.  The queue takes the requests immediately, but
    // the requests won't be processed until the queue is associated with
    // a runnning client.
    queue.start();

    Callable<Boolean> doConnect =
      new Callable<Boolean>() {
        // Executes asynchronously.

        public Boolean call() throws Exception {
          boolean connected = client.connect();  // Blocking call.
          if (connected) {
            queue.set(client);
            logger.info("Work queue attached to client " + client.getSourceId());
          } else {
            logger.fatal(String.format(
              "Cannot connect: %s. Removing queue and client",
              client.getSourceId()));
            // Here we must remove the queue and client from the
            // managed list.
            Pair<ClientWorkQueue, TWSClient> removed = removeManagedClient(
              profile, id);
            Pair<ClientWorkQueue, TWSClient> picked = null;
            int attempts = clients.size();
            for (int i = 0; i < attempts; i++) {
              synchronized (clients) {
                int r = (new Random(System.currentTimeMillis())).nextInt(clients.size());
                picked = getManagedClient(r);
              }
              if (picked != null && !picked.second.getProfile().equals(profile)) {
                break;  // Found one with the SAME profile.
              }
            }

            if (picked != null) {
              logger.info(String.format("Draining %s to %s",
                removed.second.getSourceId(), picked.second.getSourceId()));
            }
          }
          return connected;
        }
      };
    // Now start work asynchronously.  This doesn't block as the work
    // is done in the executor.
    Future<Boolean> future = this.executor.submit(doConnect);
    return new ConnectionStatus(id, future);
  }

  /**
   * Stops the connection of the given name.
   * @param name The connection name.
   * @return True if successful.
   */
  public synchronized boolean stopConnection(String name, int id) {
    if (getManagedClient(name, id) != null) {
      logger.info("Stopping connection " + makeKey(name, id));
      if (clients.containsKey(makeKey(name, id))) {
        Pair<ClientWorkQueue, TWSClient> p = removeManagedClient(name, id);
        p.first.setRunning(false);
        p.second.disconnect();
        return true;
      }
    } else {
      logger.info(String.format("Connection(%s) does not exist.",
        name));
    }
    return false;
  }

  /**
   * Returns a reference to a client. Based on round robbin.
   * @param name The name of the client.
   * @return The client.
   */
  public TWSClient getClient(String name, int id) {
    Pair<ClientWorkQueue, TWSClient> p = getManagedClient(name, id);
    if (p != null) {
      return p.second;
    }
    return null;
  }

  /**
   * Enqueue a request for the named client connection.  Starts the connection
   * on demand if client is not already running.
   * 
   * @param <V> An object type.  Special Managed object can be shutdown by service.
   * @param name The client name.
   * @param work Unit of work to be enqueued.
   */
  public <V> void enqueue(String name, Function<TWSClient, V> work) {
    boolean clientNotReady = true;
    do {
      int pick = this.roundRobbin.incrementAndGet() % this.clients.size();
      Pair<ClientWorkQueue, TWSClient> p = getManagedClient(pick);
      if (p.second.isReady()) {
        logger.info(String.format(
          "Queueing work for %s with queueSize=%d",
          p.second.getId(), p.first.getQueueDepth()));
        p.first.enqueue(work);
        return;
      }
      logger.info(p.second.getId() + " is not ready.  Try again.");
    } while (clientNotReady);
  }

  /**
   * Enqueue a request for the named client connection.  Starts the connection
   * on demand if client is not already running.
   * 
   * @param <V> An object type.  Special Managed object can be shutdown by service.
   * @param name The client profile
   * @param id The id.
   * @param work Unit of work to be enqueued.
   */
  public <V> void enqueue(String profile, int id, Function<TWSClient, V> work) {
    Pair<ClientWorkQueue, TWSClient> p = clients.get(makeKey(profile, id));
    p.first.enqueue(work);
    logger.info(String.format(
      "Queueing work for %s in %s with queueSize=%d",
      p.second.getId(), p.second.getState(), p.first.getQueueDepth()));
  }

  /**
   * Implementation of the shutdown handler for the TWSClientManager.
   * It performs the following tasks:
   * 1. Stops / disconnects all clients.
   * 2. Shuts down all associated queues.
   * 3. Stops the executor for the manager.
   */
  public static class ShutdownHandler implements Shutdown<Boolean> {

    @Inject
    TWSClientManager manager;

    public Boolean call() throws Exception {
      for (Pair<ClientWorkQueue, TWSClient> p : manager.clients.values()) {
        // Shutdown the client connection.
        TWSClient client = p.second;

        if (client != null) {
          client.disconnect();
          client.shutdown();
        }

        // Stop the work queue.
        ClientWorkQueue q = p.first;
        if (q != null) {
          q.setRunning(false);
          q.waitForStop(10, TimeUnit.SECONDS);
        }
        // Stop the managed objects for this client.
        if (manager.managed.get(client) != null) {
          for (Managed m : manager.managed.get(client)) {
            try {
              m.halt();
            } catch (Exception e) {
              logger.warn("Exception while trying to stop: ", e);
            }
          }
        }
      }
      manager.executor.shutdown();
      return true;
    }

    public String getName() {
      return "tws-api-service";
    }
  }
}
