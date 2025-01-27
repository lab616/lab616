package com.lab616.ib.scripting;


import com.google.inject.Inject;
import com.ib.client.EWrapper;
import com.lab616.common.Pair;
import com.lab616.common.scripting.ScriptObject;
import com.lab616.common.scripting.ScriptObject.ScriptModule;
import com.lab616.ib.api.TWSClient;
import com.lab616.ib.api.TWSClientManager;
import com.lab616.ib.api.simulator.EClientSocketSimulator;
import com.lab616.ib.api.simulator.EWrapperDataSource;
import com.lab616.ib.api.simulator.ProtoFileDataSource;
import com.lab616.omnibus.http.ServletScript;

/**
 * Command that plays back either CSV for Proto data files.
 * 
 * @author david
 *
 */
@ServletScript(path = "/tws/playbackdata")
@ScriptModule(name = "PlaybackData", doc = "Data playback, for back testing.")
public class PlaybackData extends ScriptObject {

	private final TWSClientManager clientManager;
	
	@Inject
	public PlaybackData(TWSClientManager clientManager) {
		this.clientManager = clientManager;
	}


  public interface PlaybackSession<T> {
    public T start();
  }

  /**
   * Builder-like interface for working with the session.
   */
  public static class Playback {

    private final EClientSocketSimulator sim;
    private final Integer clientId;
    Playback(EClientSocketSimulator sim, Integer clientId) {
      this.sim = sim;
      this.clientId = clientId;
    }

    public Integer getClientId() {
      return clientId;
    }

    public interface Source {
      public boolean finished();
    }

    public PlaybackSession<Source> withProtoFile(String fname, long... latency)
      throws Exception {
      final ProtoFileDataSource pfd = new ProtoFileDataSource(fname);
      if (latency.length > 0 && latency[0] > 0) {
        pfd.setEventLatency(latency[0]);
      }
      sim.addDataSource(pfd);
      return new PlaybackSession<Source>() {
        @Override
        public Source start() {
          pfd.start();
          return new Source() {
            @Override
            public boolean finished() {
              return pfd.finished();
            }
          };
        }
      };
    }

    public PlaybackSession<EWrapper> withEWrapper(String name) {
      final EWrapperDataSource ewd = new EWrapperDataSource(name);
      sim.addDataSource(ewd);
      return new PlaybackSession<EWrapper>() {
        @Override
        public EWrapper start() {
          EWrapper wrapper = ewd.getEWrapper();
          ewd.start();
          return wrapper;
        }
      };
    }
  }

  @ServletScript(path = "getSession")
  @Script(name = "getSession", doc = "Returns a playback session handle.")
	public final Playback getSession(
    @Parameter(name="profile", doc = "The client profile.") 
    final String profile,
    @Parameter(name="clientId", doc = "The client id.") 
    final int id) throws Exception {
    return (EClientSocketSimulator.getSimulator(profile, id) != null) ?
    new Playback(EClientSocketSimulator.getSimulator(profile, id), id) :
      newSession(profile);
  }

  @ServletScript(path = "newSession")
  @Script(name = "newSession", doc = "Creates a new playback session.")
	public final Playback newSession(
    @Parameter(name="profile", doc = "The client profile.") 
    String profile) throws Exception {
    final Pair<EClientSocketSimulator, Integer> sim = startSimulator(profile);
    return new Playback(sim.first, sim.second);
	}

  @ServletScript(path = "isQueueEmpty")
  @Script(name = "isQueueEmpty", doc = "")
  public final boolean isQueueEmpty(
      @Parameter(name="profile", doc = "The client profile.") 
  		String profile, 
      @Parameter(name="clientId", doc = "The client id.") 
      int id) {
    return EClientSocketSimulator.getSimulator(profile, id).isEventQueueEmpty();
  }

  @ServletScript(path = "getQueueDepth")
  @Script(name = "getQueueDepth", doc = "")
  public final int getQueueDepth(
      @Parameter(name="profile", doc = "The client profile.") 
  		String profile, 
      @Parameter(name="clientId", doc = "The client id.") 
      int id) {
    return EClientSocketSimulator.getSimulator(profile, id).getQueueDepth();
  }

  /**
	 * Starts the simulator with the given profile name.  A new client is
	 * started.  The profile should be distinct from the actual production / live
	 * accounts.
	 * @param profile The profile name.
	 * @return A simulator instance + clientId.
	 * @throws Exception
	 */
  Pair<EClientSocketSimulator , Integer> startSimulator(String profile) throws Exception {
		int clientId = clientManager.newConnection(profile, true).getClientId();
		
		TWSClient client = clientManager.getClient(profile, clientId);
		client.connect();
		while (!client.isReady()) {
			Thread.sleep(10L);
		}
		return Pair.of(
      EClientSocketSimulator.getSimulator(profile, clientId), clientId);
	}
	
}
