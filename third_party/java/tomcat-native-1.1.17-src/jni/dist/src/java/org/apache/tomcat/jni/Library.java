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

/** Library
 *
 * @author Mladen Turk
 * @version $Revision: 671459 $, $Date: 2008-06-25 10:14:44 +0200 (Wed, 25 Jun 2008) $
 */

public final class Library {

    /* Default library names */
    private static String [] NAMES = {"tcnative-1", "libtcnative-1"};
    /*
     * A handle to the unique Library singleton instance.
     */
    static private Library _instance = null;

    private Library()
        throws Exception
    {
        boolean loaded = false;
        String err = "";
        for (int i = 0; i < NAMES.length; i++) {
            try {
                System.loadLibrary(NAMES[i]);
                loaded = true;
            }
            catch (Throwable e) {
                String name = System.mapLibraryName(NAMES[i]);
                String path = System.getProperty("java.library.path");
                String sep = System.getProperty("path.separator");
                String [] paths = path.split(sep);
                for (int j=0; j<paths.length; j++) {
                    java.io.File fd = new java.io.File(paths[j] , name);
                    if (fd.exists()) {
                        e.printStackTrace();
                    }
                }
                if ( i > 0)
                    err += ", ";
                err +=  e.getMessage();
            }
            if (loaded)
                break;
        }
        if (!loaded) {
            err += "(";
            err += System.getProperty("java.library.path");
            err += ")";
            throw new UnsatisfiedLinkError(err);
        }
    }

    private Library(String libraryName)
    {
        System.loadLibrary(libraryName);
    }

    /* create global TCN's APR pool
     * This has to be the first call to TCN library.
     */
    private static native boolean initialize();
    /* destroy global TCN's APR pool
     * This has to be the last call to TCN library.
     */
    public static native void terminate();
    /* Internal function for loading APR Features */
    private static native boolean has(int what);
    /* Internal function for loading APR Features */
    private static native int version(int what);
    /* Internal function for loading APR sizes */
    private static native int size(int what);

    /* TCN_MAJOR_VERSION */
    public static int TCN_MAJOR_VERSION  = 0;
    /* TCN_MINOR_VERSION */
    public static int TCN_MINOR_VERSION  = 0;
    /* TCN_PATCH_VERSION */
    public static int TCN_PATCH_VERSION  = 0;
    /* TCN_IS_DEV_VERSION */
    public static int TCN_IS_DEV_VERSION = 0;
    /* APR_MAJOR_VERSION */
    public static int APR_MAJOR_VERSION  = 0;
    /* APR_MINOR_VERSION */
    public static int APR_MINOR_VERSION  = 0;
    /* APR_PATCH_VERSION */
    public static int APR_PATCH_VERSION  = 0;
    /* APR_IS_DEV_VERSION */
    public static int APR_IS_DEV_VERSION = 0;

    /* TCN_VERSION_STRING */
    public static native String versionString();
    /* APR_VERSION_STRING */
    public static native String aprVersionString();

    /*  APR Feature Macros */
    public static boolean APR_HAVE_IPV6           = false;
    public static boolean APR_HAS_SHARED_MEMORY   = false;
    public static boolean APR_HAS_THREADS         = false;
    public static boolean APR_HAS_SENDFILE        = false;
    public static boolean APR_HAS_MMAP            = false;
    public static boolean APR_HAS_FORK            = false;
    public static boolean APR_HAS_RANDOM          = false;
    public static boolean APR_HAS_OTHER_CHILD     = false;
    public static boolean APR_HAS_DSO             = false;
    public static boolean APR_HAS_SO_ACCEPTFILTER = false;
    public static boolean APR_HAS_UNICODE_FS      = false;
    public static boolean APR_HAS_PROC_INVOKED    = false;
    public static boolean APR_HAS_USER            = false;
    public static boolean APR_HAS_LARGE_FILES     = false;
    public static boolean APR_HAS_XTHREAD_FILES   = false;
    public static boolean APR_HAS_OS_UUID         = false;
    /* Are we big endian? */
    public static boolean APR_IS_BIGENDIAN        = false;
    /* APR sets APR_FILES_AS_SOCKETS to 1 on systems where it is possible
     * to poll on files/pipes.
     */
    public static boolean APR_FILES_AS_SOCKETS    = false;
    /* This macro indicates whether or not EBCDIC is the native character set.
     */
    public static boolean APR_CHARSET_EBCDIC      = false;
    /* Is the TCP_NODELAY socket option inherited from listening sockets?
     */
    public static boolean APR_TCP_NODELAY_INHERITED = false;
    /* Is the O_NONBLOCK flag inherited from listening sockets?
     */
    public static boolean APR_O_NONBLOCK_INHERITED  = false;


    public static int APR_SIZEOF_VOIDP;
    public static int APR_PATH_MAX;
    public static int APRMAXHOSTLEN;
    public static int APR_MAX_IOVEC_SIZE;
    public static int APR_MAX_SECS_TO_LINGER;
    public static int APR_MMAP_THRESHOLD;
    public static int APR_MMAP_LIMIT;

    /* return global TCN's APR pool */
    public static native long globalPool();

    /**
     * Setup any APR internal data structures.  This MUST be the first function
     * called for any APR library.
     * @param libraryName the name of the library to load
     */
    static public boolean initialize(String libraryName)
        throws Exception
    {
        if (_instance == null) {
            if (libraryName == null)
                _instance = new Library();
            else
                _instance = new Library(libraryName);
            TCN_MAJOR_VERSION  = version(0x01);
            TCN_MINOR_VERSION  = version(0x02);
            TCN_PATCH_VERSION  = version(0x03);
            TCN_IS_DEV_VERSION = version(0x04);
            APR_MAJOR_VERSION  = version(0x11);
            APR_MINOR_VERSION  = version(0x12);
            APR_PATCH_VERSION  = version(0x13);
            APR_IS_DEV_VERSION = version(0x14);

            APR_SIZEOF_VOIDP        = size(1);
            APR_PATH_MAX            = size(2);
            APRMAXHOSTLEN           = size(3);
            APR_MAX_IOVEC_SIZE      = size(4);
            APR_MAX_SECS_TO_LINGER  = size(5);
            APR_MMAP_THRESHOLD      = size(6);
            APR_MMAP_LIMIT          = size(7);

            APR_HAVE_IPV6           = has(0);
            APR_HAS_SHARED_MEMORY   = has(1);
            APR_HAS_THREADS         = has(2);
            APR_HAS_SENDFILE        = has(3);
            APR_HAS_MMAP            = has(4);
            APR_HAS_FORK            = has(5);
            APR_HAS_RANDOM          = has(6);
            APR_HAS_OTHER_CHILD     = has(7);
            APR_HAS_DSO             = has(8);
            APR_HAS_SO_ACCEPTFILTER = has(9);
            APR_HAS_UNICODE_FS      = has(10);
            APR_HAS_PROC_INVOKED    = has(11);
            APR_HAS_USER            = has(12);
            APR_HAS_LARGE_FILES     = has(13);
            APR_HAS_XTHREAD_FILES   = has(14);
            APR_HAS_OS_UUID         = has(15);
            APR_IS_BIGENDIAN        = has(16);
            APR_FILES_AS_SOCKETS    = has(17);
            APR_CHARSET_EBCDIC      = has(18);
            APR_TCP_NODELAY_INHERITED = has(19);
            APR_O_NONBLOCK_INHERITED  = has(20);
            if (APR_MAJOR_VERSION < 1) {
                throw new UnsatisfiedLinkError("Unsupported APR Version (" +
                                               aprVersionString() + ")");
            }
            if (!APR_HAS_THREADS) {
                throw new UnsatisfiedLinkError("Missing APR_HAS_THREADS");
            }
        }
        return initialize();
    }
}
