// 2009 lab616.com, All Rights Reserved.

package com.lab616.omnibus.event;

import com.lab616.common.Converter;

/**
 * Event within the system.  This is basically a generalized form of an object
 * method call, with instance name (e.g. account/client), method (api method)
 * and args (method args), plus some additional metadata such as timestamp and
 * source/ destination.
 */
public final class EventMessage {

	public static final String EVENT_NAME = "EventMessage";
	
	public final String source;
	public final String destination;
	
  public final String instance;
  public final String method;
  public final Object[] args;
  public final long timestamp;

	public EventMessage(String fromString, String toString,
			String instance, String method, Object[] args, long timestamp) {
		source = fromString;
		destination = toString;
		this.instance = instance;
		this.method = method;
		this.args = args;
		this.timestamp = timestamp;
	}
	
	public String getSource() {
		return source;
	}
	
	public String getDestination() {
		return destination;
	}
	
	public String getMethod() {
		return method;
	}
	
	public Object[] getArgs() {
		return args;
	}
	
	public long getTimestamp() {
		return timestamp;
	}
	
	public String getInstance() {
		return instance;
	}
	
	public String toString() {
	  StringBuffer buff = new StringBuffer();
	  buff.append(Converter.ISO8601.print(timestamp));
	  buff.append(",");
    buff.append(source);
    buff.append(",");
    buff.append(destination);
    buff.append(",");
    buff.append(instance);
    buff.append(",");
    buff.append(method);
    for (Object o : args) {
      buff.append(",");
      buff.append(o);
    }
	  return buff.toString();
	}
}
