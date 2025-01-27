// 2009 lab616.com, All Rights Reserved.

package com.lab616.monitoring;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * @author david
 *
 */
@Target(ElementType.FIELD)
@Retention(RetentionPolicy.RUNTIME)
public @interface Varz {
	
//  final static String EMPTY_VALUE = "";

  /**
	 * Name of the flag.
	 * @return The name.
	 */
	String name();

	/**
	 * Doc string, useful for printing help messages.
	 * @return The doc string.
	 */
	String doc() default "";
}
