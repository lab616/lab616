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

/** SSL Socket
 *
 * @author Mladen Turk
 * @version $Revision: 466585 $, $Date: 2006-10-22 00:16:34 +0200 (Sun, 22 Oct 2006) $
 */

public class SSLSocket {

    /**
     * Attach APR socket on a SSL connection.
     * @param ctx SSLContext to use.
     * @param sock APR Socket that already did physical connect or accept.
     * @return APR_STATUS code.
     */
    public static native int attach(long ctx, long sock)
        throws Exception;

    /**
     * Do a SSL handshake.
     * @param thesocket The socket to use
     */
    public static native int handshake(long thesocket);

    /**
     * Do a SSL renegotiation.
     * SSL supports per-directory re-configuration of SSL parameters.
     * This is implemented by performing an SSL renegotiation of the
     * re-configured parameters after the request is read, but before the
     * response is sent. In more detail: the renegotiation happens after the
     * request line and MIME headers were read, but _before_ the attached
     * request body is read. The reason simply is that in the HTTP protocol
     * usually there is no acknowledgment step between the headers and the
     * body (there is the 100-continue feature and the chunking facility
     * only), so Apache has no API hook for this step.
     *
     * @param thesocket The socket to use
     */
    public static native int renegotiate(long thesocket);

    /**
     * Retrun SSL Info parameter as byte array.
     *
     * @param sock The socket to read the data from.
     * @param id Parameter id.
     * @return Byte array containing info id value.
     */
    public static native byte[] getInfoB(long sock, int id)
        throws Exception;

    /**
     * Retrun SSL Info parameter as String.
     *
     * @param sock The socket to read the data from.
     * @param id Parameter id.
     * @return String containing info id value.
     */
    public static native String getInfoS(long sock, int id)
        throws Exception;

    /**
     * Retrun SSL Info parameter as integer.
     *
     * @param sock The socket to read the data from.
     * @param id Parameter id.
     * @return Integer containing info id value or -1 on error.
     */
    public static native int getInfoI(long sock, int id)
        throws Exception;

}
