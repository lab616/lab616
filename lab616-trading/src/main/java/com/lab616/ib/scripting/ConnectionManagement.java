/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */
package com.lab616.ib.scripting;

import java.util.concurrent.TimeUnit;

import org.apache.log4j.Logger;

import com.google.inject.Inject;
import com.lab616.common.scripting.ScriptException;
import com.lab616.common.scripting.ScriptObject;
import com.lab616.common.scripting.ScriptObject.ScriptModule;
import com.lab616.ib.api.TWSClientManager;
import com.lab616.ib.api.TWSClientManager.ConnectionStatus;
import com.lab616.omnibus.http.ServletScript;

/**
 *
 * @author dchung
 */
@ServletScript(path = "/tws/connection")
@ScriptModule(name = "ConnectionManagement",
doc = "Basic scripts for managing TWS client connections.")
public class ConnectionManagement extends ScriptObject {

  private static Logger logger = Logger.getLogger(ConnectionManagement.class);
  private final TWSClientManager clientManager;

  @Inject
  public ConnectionManagement(TWSClientManager clientManager) {
    this.clientManager = clientManager;
  }

  /**
   * Starts a new connection profile.
   * @param profile The name of the profile.
   * @param hostName Hostname.
   * @param port Port number.
   */
  @ServletScript(path = "np")
  @Script(name = "newProfile",
  doc = "Adds a new profile which maps a host:port to a profile name.")
  public void newProfile(
  		@Parameter(name = "profile", doc = "The connection profile.")
  		String profile, 
  		@Parameter(name = "host", defaultValue = "localhost", doc = "TWS host.")
  		String hostName, 
  		@Parameter(name = "port", defaultValue = "7496",  doc = "TWS port.")
  		int port) {
    logger.info(String.format(
      "START: newProfile with profile=%s, hostName=%s, port=%d",
      profile, hostName, port));
    try {
      if (!this.clientManager.profileExists(profile)) {
        this.clientManager.addProfile(profile, hostName, port);
      }
    } catch (Exception e) {
      throw new ScriptException(this, e,
        "Exception adding new profile %s @ (%s:%d)", profile, hostName, port);
    }
    logger.info(String.format(
      "OK: newProfile with profile=%s, hostName=%s, port=%d",
      profile, hostName, port));
  }
  
  
  /**
   * Starts a new connection by profile name.
   * @param profile The profile name.
   * @param timeoutMillis Timeout in milliseconds for the client to be ready.
   * @return The client id.
   */
  @ServletScript(path = "nc")
  @Script(name = "newConnection",
  doc = "Adds a new connection with the given profile, returns the client id.")
  public int newConnection(
  		@Parameter(name = "profile", doc = "The connection profile.")
  		String profile, 
  		@Parameter(name = "timeout", defaultValue = "10000", doc = "The timeout in millis.")
  		long timeoutMillis) {
    if (!this.clientManager.profileExists(profile)) {
      throw new ScriptException(this, "Unkown profile: %s", profile);
    }

    long timeout = (timeoutMillis > 0) ? timeoutMillis : 100L; // Min 100 msec.
    long sleep = 50L;
    long waits = timeout / sleep; // Number of waits.
    int clientId = -1;
    logger.info(String.format(
      "START: newConnection with profile=%s, timeoutMillis=%d, waits=%d",
      profile, timeout, waits));
    ConnectionStatus status = null;
    try {
    	status = this.clientManager.newConnection(profile, false);
    	status.isConnected(timeoutMillis, TimeUnit.MILLISECONDS);
      
    	logger.info(String.format(
          "OK: newConnection with profile=%s, timeoutMillis=%d, waits=%d",
          profile, timeout, waits));

      return clientId = status.getClientId();
    } catch (Exception e) {
      throw new ScriptException(this, e,
        "Exception while starting up client (%s@%d): %s",
        profile, clientId, e);
    }
  }

  /**
   * Destroys a client connection identified by the profile and client id.
   * @param profile The profile.
   * @param clientId The client id.
   */
  @ServletScript(path = "dc")
  @Script(name = "destroyConnection",
  doc = "Destroys the client connection of given profile and client id.")
  public void destroyConnection(
  		@Parameter(name = "profile", doc = "The connection profile.")
  		String profile, 
  		@Parameter(name = "clientId", doc = "The client id.")
  		int clientId) {
    if (this.clientManager.getClient(profile, clientId) == null) {
      throw new ScriptException(this, 
        "No such client %s@%d", profile, clientId);
    }
    logger.info(String.format(
      "START: destroyConnection with profile=%s, clientId=%d",
      profile, clientId));
    boolean disconnected = false;
    try {
      disconnected = this.clientManager.stopConnection(profile, clientId);
    } catch (Exception e) {
      throw new ScriptException(this, e,
        "Exception while destroying client %s@%d", profile, clientId);
    }
    if (!disconnected) {
      throw new ScriptException(this,
        "Unable to disconnect client %s@%d", profile, clientId);
    }
    logger.info(String.format(
      "OK: destroyConnection with profile=%s, clientId=%d",
      profile, clientId));
  }
}
