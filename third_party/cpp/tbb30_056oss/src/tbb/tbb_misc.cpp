/*
    Copyright 2005-2010 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

// Source file for miscellaneous entities that are infrequently referenced by 
// an executing program.

#include "tbb/tbb_stddef.h"
#include "tbb_assert_impl.h" // Out-of-line TBB assertion handling routines are instantiated here.
#include "tbb/tbb_exception.h"
#include "tbb/tbb_machine.h"
#include "tbb_misc.h"
#include <cstdio>
#include <cstdlib>

#if !TBB_USE_EXCEPTIONS && _MSC_VER
    // Suppress "C++ exception handler used, but unwind semantics are not enabled" warning in STL headers
    #pragma warning (push)
    #pragma warning (disable: 4530)
#endif

#include <cstring>

#if !TBB_USE_EXCEPTIONS && _MSC_VER
    #pragma warning (pop)
#endif

using namespace std;

namespace tbb {

const char* bad_last_alloc::what() const throw() { return "bad allocation in previous or concurrent attempt"; }
const char* improper_lock::what() const throw() { return "attempted recursive lock on critical section or non-recursive mutex"; }
const char* invalid_multiple_scheduling::what() const throw() { return "The same task_handle object cannot be executed more than once"; }
const char* missing_wait::what() const throw() { return "wait() was not called on the structured_task_group"; }

namespace internal {

#if TBB_USE_EXCEPTIONS
    #define DO_THROW(exc, init_args) throw exc init_args;
#else /* !TBB_USE_EXCEPTIONS */
    #define PRINT_ERROR_AND_ABORT(exc_name, msg) \
        fprintf (stderr, "Exception %s with message %s would've been thrown, "  \
            "if exception handling were not disabled. Aborting.\n", exc_name, msg); \
        fflush(stderr); \
        abort();
    #define DO_THROW(exc, init_args) PRINT_ERROR_AND_ABORT(#exc, #init_args)
#endif /* !TBB_USE_EXCEPTIONS */


/* The "what" should be fairly short, not more than about 128 characters.
   Because we control all the call sites to handle_perror, it is pointless
   to bullet-proof it for very long strings.

   Design note: ADR put this routine off to the side in tbb_misc.cpp instead of
   Task.cpp because the throw generates a pathetic lot of code, and ADR wanted
   this large chunk of code to be placed on a cold page. */
void handle_perror( int error_code, const char* what ) {
    char buf[256];
    __TBB_ASSERT( strlen(what) < sizeof(buf) - 64, "Error description is too long" );
    sprintf(buf,"%s: ",what);
    char* end = strchr(buf,0);
    size_t n = buf+sizeof(buf)-end;
    strncpy( end, strerror( error_code ), n );
    // Ensure that buffer ends in terminator.
    buf[sizeof(buf)-1] = 0; 
#if TBB_USE_EXCEPTIONS
    throw runtime_error(buf);
#else
    PRINT_ERROR_AND_ABORT( "runtime_error", buf);
#endif /* !TBB_USE_EXCEPTIONS */
}

#if _WIN32||_WIN64 
void handle_win_error( int error_code ) {
    char buf[512];
    FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, error_code, 0, buf, sizeof(buf), NULL );
#if TBB_USE_EXCEPTIONS
    throw runtime_error(buf);
#else
    PRINT_ERROR_AND_ABORT( "runtime_error", buf);
#endif /* !TBB_USE_EXCEPTIONS */
}
#endif // _WIN32||_WIN64

void throw_bad_last_alloc_exception_v4() {
    throw_exception_v4(eid_bad_last_alloc);
}

void throw_exception_v4 ( exception_id eid ) {
    __TBB_ASSERT ( eid > 0 && eid < eid_max, "Unknown exception ID" );
    switch ( eid ) {
    case eid_bad_alloc: DO_THROW( bad_alloc, () );
    case eid_bad_last_alloc: DO_THROW( bad_last_alloc, () );
    case eid_nonpositive_step: DO_THROW( invalid_argument, ("Step must be positive") );
    case eid_out_of_range: DO_THROW( out_of_range, ("Index out of requested size range") );
    case eid_segment_range_error: DO_THROW( range_error, ("Index out of allocated segment slots") );
    case eid_index_range_error: DO_THROW( range_error, ("Index is not allocated") );
    case eid_missing_wait: DO_THROW( missing_wait, () );
    case eid_invalid_multiple_scheduling: DO_THROW( invalid_multiple_scheduling, () );
    case eid_improper_lock: DO_THROW( improper_lock, () );
    case eid_possible_deadlock: DO_THROW( runtime_error, ("Resource deadlock would occur") );
    case eid_operation_not_permitted: DO_THROW( runtime_error, ("Operation not permitted") );
    case eid_condvar_wait_failed: DO_THROW( runtime_error, ("Wait on condition variable failed") );
    case eid_invalid_load_factor: DO_THROW( out_of_range, ("Invalid hash load factor") );
    case eid_reserved: DO_THROW( out_of_range, ("[backward compatibility] Invalid number of buckets") );
    case eid_invalid_swap: DO_THROW( invalid_argument, ("swap() is invalid on non-equal allocators") );
    case eid_reservation_length_error: DO_THROW( length_error, ("reservation size exceeds permitted max size") );
    case eid_invalid_key: DO_THROW( out_of_range, ("invalid key") );
    default: break;
    }
#if !TBB_USE_EXCEPTIONS && __APPLE__
    out_of_range e1("");
    length_error e2("");
    range_error e3("");
    invalid_argument e4("");
#endif /* !TBB_USE_EXCEPTIONS && __APPLE__ */
}

#if _XBOX
bool GetBoolEnvironmentVariable( const char * name ) { return false;}
#else
bool GetBoolEnvironmentVariable( const char * name ) {
    if( const char* s = getenv(name) )
        return strcmp(s,"0") != 0;
    return false;
}
#endif /* !_XBOX */

#include "tbb_version.h"

/** The leading "\0" is here so that applying "strings" to the binary delivers a clean result. */
static const char VersionString[] = "\0" TBB_VERSION_STRINGS;

static bool PrintVersionFlag = false;

void PrintVersion() {
    PrintVersionFlag = true;
    fputs(VersionString+1,stderr);
}

void PrintExtraVersionInfo( const char* category, const char* description ) {
    if( PrintVersionFlag ) 
        fprintf(stderr, "%s: %s\t%s\n", "TBB", category, description );
}

void PrintRMLVersionInfo( void* arg, const char* server_info ) {
    PrintExtraVersionInfo( server_info, (const char *)arg );
}

} // namespace internal
 
extern "C" int TBB_runtime_interface_version() {
    return TBB_INTERFACE_VERSION;
}

} // namespace tbb

#if !__TBB_RML_STATIC
#if __TBB_x86_32

#include "tbb/atomic.h"

// in MSVC environment, int64_t defined in tbb::internal namespace only (see tbb_stddef.h)
#if _MSC_VER
using tbb::internal::int64_t;
#endif

//! Warn about 8-byte store that crosses a cache line.
extern "C" void __TBB_machine_store8_slow_perf_warning( volatile void *ptr ) {
    // Report run-time warning unless we have already recently reported warning for that address.
    const unsigned n = 4;
    static tbb::atomic<void*> cache[n];
    static tbb::atomic<unsigned> k;
    for( unsigned i=0; i<n; ++i ) 
        if( ptr==cache[i] ) 
            goto done;
    cache[(k++)%n] = const_cast<void*>(ptr);
    tbb::internal::runtime_warning( "atomic store on misaligned 8-byte location %p is slow", ptr );
done:;
}

//! Handle 8-byte store that crosses a cache line.
extern "C" void __TBB_machine_store8_slow( volatile void *ptr, int64_t value ) {
    for( tbb::internal::atomic_backoff b;; b.pause() ) {
        int64_t tmp = *(int64_t*)ptr;
        if( __TBB_machine_cmpswp8(ptr,value,tmp)==tmp ) 
            break;
    }
}

#endif /* __TBB_x86_32 */
#endif /* !__TBB_RML_STATIC */

#if __TBB_ipf
extern "C" intptr_t __TBB_machine_lockbyte( volatile unsigned char& flag ) {
    if ( !__TBB_TryLockByte(flag) ) {
        tbb::internal::atomic_backoff b;
        do {
            b.pause();
        } while ( !__TBB_TryLockByte(flag) );
    }
    return 0;
}
#endif
