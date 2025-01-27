/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _FF_UTILS_HPP_
#define _FF_UTILS_HPP_
/* ***************************************************************************
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License version 3 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 ****************************************************************************
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <cycle.h>
#include <iostream>
#include <pthread.h>

namespace ff {

enum { START_TIME=0, STOP_TIME=1, GET_TIME=2 };

static inline ticks ticks_wait(ticks t1) {
    ticks delta;
    ticks t0 = getticks();
    do { delta = getticks()-t0; } while (delta < t1);
    return delta-t1;
}

static inline void error(const char * str, ...) {
    const char err[]="ERROR: ";
    va_list argp;
    char * p=(char *)malloc(strlen(str)+strlen(err)+10);
    if (!p) abort();
    strcpy(p,err);
    strcpy(p+strlen(err), str);
    va_start(argp, str);
    vfprintf(stderr, p, argp);
    va_end(argp);
    free(p);
}

// return current time in usec 
static inline unsigned long getusec() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (unsigned long)(tv.tv_sec*1e6+tv.tv_usec);
}

// compute a-b and return the difference in msec
static inline const double diffmsec(const struct timeval & a, 
                                    const struct timeval & b) {
    long sec  = (a.tv_sec  - b.tv_sec);
    long usec = (a.tv_usec - b.tv_usec);
    
    if(usec < 0) {
        --sec;
        usec += 1000000;
    }
    return ((double)(sec*1000)+ (double)usec/1000.0);
}

static inline bool time_compare(struct timeval & a, struct timeval & b) {
    double t1= a.tv_sec*1000 + (double)(a.tv_usec)/1000.0;
    double t2= b.tv_sec*1000 + (double)(b.tv_usec)/1000.0;        
    return (t1<t2);
}


static inline double ffTime(int tag) {
    static struct timeval tv_start = {0,0};
    static struct timeval tv_stop  = {0,0};

    double res=0.0;
    switch(tag) {
    case START_TIME:{
        gettimeofday(&tv_start,NULL);
    } break;
    case STOP_TIME:{
        gettimeofday(&tv_stop,NULL);
        res = diffmsec(tv_stop,tv_start);
    } break;
    case GET_TIME: {
        res = diffmsec(tv_stop,tv_start);
    } break;
    default:
        res=0;
    }    
    return res;
}


} // namespace ff


#endif /* _FF_UTILS_HPP_ */
