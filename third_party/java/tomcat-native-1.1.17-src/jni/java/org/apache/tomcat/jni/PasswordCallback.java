/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package org.apache.tomcat.jni;

/** PasswordCallback Interface
 *
 * @author Mladen Turk
 * @version $Revision: 466585 $, $Date: 2006-10-22 00:16:34 +0200 (Sun, 22 Oct 2006) $
 */

public interface PasswordCallback {

    /**
     * Called when the password is required
     * @param prompt Password prompt
     * @return Valid password or null
     */
    public String callback(String prompt);
}
