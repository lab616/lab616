// Copyright (c) 2009, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Craig Silverstein
//
// This forks out to pprof to do the actual symbolizing.  We might
// be better off writing our own in C++.

#include "config.h"
#include "symbolize.h"
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>   // for write()
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>   // for socketpair() -- needed by Symbolize
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>   // for wait() -- needed by Symbolize
#endif
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#include <string>
#include "base/commandlineflags.h"
#include "base/sysinfo.h"

using std::string;
using tcmalloc::DumpProcSelfMaps;   // from sysinfo.h


DEFINE_string(symbolize_pprof,
              EnvToString("PPROF_PATH", "pprof"),
              "Path to pprof to call for reporting function names.");

// heap_profile_table_pprof may be referenced after destructors are
// called (since that's when leak-checking is done), so we make
// a more-permanent copy that won't ever get destroyed.
static string* g_pprof_path = new string(FLAGS_symbolize_pprof);

void SymbolTable::Add(const void* addr) {
  symbolization_table_[addr] = "";
}

const char* SymbolTable::GetSymbol(const void* addr) {
  return symbolization_table_[addr];
}

// Updates symbolization_table with the pointers to symbol names corresponding
// to its keys. The symbol names are stored in out, which is allocated and
// freed by the caller of this routine.
// Note that the forking/etc is not thread-safe or re-entrant.  That's
// ok for the purpose we need -- reporting leaks detected by heap-checker
// -- but be careful if you decide to use this routine for other purposes.
int SymbolTable::Symbolize() {
#if !defined(HAVE_UNISTD_H)  || !defined(HAVE_SYS_SOCKET_H) || !defined(HAVE_SYS_WAIT_H)
  return 0;
#elif !defined(HAVE_PROGRAM_INVOCATION_NAME)
  return 0;   // TODO(csilvers): get argv[0] somehow
#else
  // All this work is to do two-way communication.  ugh.
  extern char* program_invocation_name;  // gcc provides this
  int child_in[2];   // file descriptors
  int child_out[2];  // for now, we don't worry about child_err
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, child_in) == -1) {
    return 0;
  }
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, child_out) == -1) {
    close(child_in[0]);
    close(child_in[1]);
    return 0;
  }
  switch (fork()) {
    case -1: {  // error
      close(child_in[0]);
      close(child_in[1]);
      close(child_out[0]);
      close(child_out[1]);
      return 0;
    }
    case 0: {  // child
      close(child_in[1]);   // child uses the 0's, parent uses the 1's
      close(child_out[1]);  // child uses the 0's, parent uses the 1's
      close(0);
      close(1);
      if (dup2(child_in[0], 0) == -1) _exit(1);
      if (dup2(child_out[0], 1) == -1) _exit(2);
      // Unset vars that might cause trouble when we fork
      unsetenv("CPUPROFILE");
      unsetenv("HEAPPROFILE");
      unsetenv("HEAPCHECK");
      unsetenv("PERFTOOLS_VERBOSE");
      execlp(g_pprof_path->c_str(), g_pprof_path->c_str(),
             "--symbols", program_invocation_name, NULL);
      _exit(3);  // if execvp fails, it's bad news for us
    }
    default: {  // parent
      close(child_in[0]);   // child uses the 0's, parent uses the 1's
      close(child_out[0]);  // child uses the 0's, parent uses the 1's
#ifdef HAVE_POLL_H
      // For maximum safety, we check to make sure the execlp
      // succeeded before trying to write.  (Otherwise we'll get a
      // SIGPIPE.)  For systems without poll.h, we'll just skip this
      // check, and trust that the user set PPROF_PATH correctly!
      struct pollfd pfd = { child_in[1], POLLOUT, 0 };
      if (!poll(&pfd, 1, 0) || !(pfd.revents & POLLOUT) ||
          (pfd.revents & (POLLHUP|POLLERR))) {
        return 0;
      }
#endif
      DumpProcSelfMaps(child_in[1]);  // what pprof expects on stdin

      // Allocate 24 bytes = ("0x" + 8 bytes + "\n" + overhead) for each
      // address to feed to pprof.
      const int kOutBufSize = 24 * symbolization_table_.size();
      char *pprof_buffer = new char[kOutBufSize];
      int written = 0;
      for (SymbolMap::const_iterator iter = symbolization_table_.begin();
           iter != symbolization_table_.end(); ++iter) {
        written += snprintf(pprof_buffer + written, kOutBufSize - written,
                 // pprof expects format to be 0xXXXXXX
                 "0x%"PRIxPTR"\n", reinterpret_cast<uintptr_t>(iter->first));
      }
      write(child_in[1], pprof_buffer, strlen(pprof_buffer));
      close(child_in[1]);             // that's all we need to write

      const int kSymbolBufferSize = kSymbolSize * symbolization_table_.size();
      int total_bytes_read = 0;
      delete[] symbol_buffer_;
      symbol_buffer_ = new char[kSymbolBufferSize];
      memset(symbol_buffer_, '\0', kSymbolBufferSize);
      while (1) {
        int bytes_read = read(child_out[1], symbol_buffer_ + total_bytes_read,
                              kSymbolBufferSize - total_bytes_read);
        if (bytes_read < 0) {
          close(child_out[1]);
          return 0;
        } else if (bytes_read == 0) {
          close(child_out[1]);
          wait(NULL);
          break;
        } else {
          total_bytes_read += bytes_read;
        }
      }
      // We have successfully read the output of pprof into out.  Make sure
      // the last symbol is full (we can tell because it ends with a \n).
      if (total_bytes_read == 0 || symbol_buffer_[total_bytes_read - 1] != '\n')
        return 0;
      // make the symbolization_table_ values point to the output vector
      SymbolMap::iterator fill = symbolization_table_.begin();
      int num_symbols = 0;
      const char *current_name = symbol_buffer_;
      for (int i = 0; i < total_bytes_read; i++) {
        if (symbol_buffer_[i] == '\n') {
          fill->second = current_name;
          symbol_buffer_[i] = '\0';
          current_name = symbol_buffer_ + i + 1;
          fill++;
          num_symbols++;
        }
      }
      return num_symbols;
    }
  }
  return 0;  // shouldn't be reachable
#endif
}
