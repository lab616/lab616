// 2009 lab616.com, All Rights Reserved.

package com.lab616.omnibus.event.watchers;

import org.apache.log4j.Logger;

import com.lab616.omnibus.event.EventWatcher;

/**
 * Stats watcher.
 * @author david
 *
 */
public class SystemEventStats extends EventWatcher<Long> {

	static Logger logger = Logger.getLogger(SystemEventStats.class);
	
	private String component;
	
	public SystemEventStats(String component, long millis) {
		super(
				"select count(*) from SystemEvent.win:time(?) where component = ?", 
				millis * 1000L, component);
		this.component = component;
	}
	
	/**
	 * Update method required to implement esper subscriber object.
	 * @param event The event.
	 */
	@Override
	public void update(Long count) {
		logger.info("Stat: " + component + "/" + count);
	}
}