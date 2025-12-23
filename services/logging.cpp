/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
    This file is part of Icecream.

    Copyright (c) 2004 Stephan Kulow <coolo@suse.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <config.h>
#include <iostream>
#include "logging.h"
#include <fstream>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>
#include <mutex>
#ifdef __linux__
#include <dlfcn.h>
#endif

using namespace std;

// Mutex for localtime() when localtime_r is not available
static mutex localtime_mutex;

// Thread-safe output_date implementation with per-second caching
std::ostream &output_date(std::ostream &os)
{
    time_t t = time(0);
    
    // Thread-local cache: stores formatted time for the current second
    thread_local time_t cached_time = 0;
    thread_local char cached_buf[64];
    
    // Only format time if the second has changed
    if (t != cached_time) {
        struct tm tm_buf;
        struct tm *tmp;
        
#ifdef HAVE_LOCALTIME_R
        // Use thread-safe localtime_r if available
        tmp = localtime_r(&t, &tm_buf);
#else
        // Fall back to localtime with mutex protection
        {
            lock_guard<mutex> lock(localtime_mutex);
            tmp = localtime(&t);
            if (tmp) {
                tm_buf = *tmp;
                tmp = &tm_buf;
            }
        }
#endif
        
        if (tmp) {
            strftime(cached_buf, sizeof(cached_buf), "%Y-%m-%d %T: ", tmp);
            cached_time = t;
        } else {
            // Fallback if time formatting fails
            snprintf(cached_buf, sizeof(cached_buf), "[time error]: ");
            cached_time = 0;
        }
    }

    if (logfile_prefix.size()) {
        os << logfile_prefix;
    }

    os << "[" << getpid() << "] ";
    os << cached_buf;
    
    return os;
}

int debug_level = Error;
ostream *logfile_trace = nullptr;
ostream *logfile_info = nullptr;
ostream *logfile_warning = nullptr;
ostream *logfile_error = nullptr;
string logfile_prefix;
volatile sig_atomic_t reset_debug_needed = 0;

static ofstream logfile_null("/dev/null");
static ofstream logfile_file;
static string logfile_filename;

static void reset_debug_signal_handler(int);

// Implementation of an iostream helper that allows redirecting output to a given file descriptor.
// This seems to be the only portable way to do it.
namespace
{
class ofdbuf : public streambuf
{
public:
    explicit ofdbuf( int fd ) : fd( fd ) {}
    virtual int_type overflow( int_type c );
    virtual streamsize xsputn( const char* c, streamsize n );
private:
    int fd;
};

ofdbuf::int_type ofdbuf::overflow( int_type c )
{
    if( c != EOF ) {
        char cc = c;
        if( write( fd, &cc, 1 ) != 1 )
            return EOF;
    }
    return c;
}

streamsize ofdbuf::xsputn( const char* c, streamsize n )
{
    return write( fd, c, n );
}

ostream* ccache_stream( int fd )
{
    int status = fcntl( fd, F_GETFL );
    if( status < 0 || ( status & ( O_WRONLY | O_RDWR )) == 0 ) {
        // As logging is not set up yet, this will log to stderr.
        log_warning() << "UNCACHED_ERR_FD provides an invalid file descriptor, using stderr" << endl;
        return &cerr; // fd is not valid fd for writting
    }
    static ofdbuf buf( fd );
    static ostream stream( &buf );
    return &stream;
}
} // namespace

void setup_debug(int level, const string &filename, const string &prefix)
{
    debug_level = level;
    logfile_prefix = prefix;
    logfile_filename = filename;

    if (logfile_file.is_open()) {
        logfile_file.close();
    }

    ostream *output = nullptr;

    if (filename.length()) {
        logfile_file.clear();
        logfile_file.open(filename.c_str(), fstream::out | fstream::app);
#ifdef __linux__

        string fname = filename;
        if (fname[0] != '/') {
            char buf[PATH_MAX];

            if (getcwd(buf, sizeof(buf))) {
                fname.insert(0, "/");
                fname.insert(0, buf);
            }
        }

        setenv("SEGFAULT_OUTPUT_NAME", fname.c_str(), false);
#endif
        output = &logfile_file;
    } else if( const char* ccache_err_fd = getenv( "UNCACHED_ERR_FD" )) {
        output = ccache_stream( atoi( ccache_err_fd ));
    } else {
        output = &cerr;
    }

#ifdef __linux__
    (void) dlopen("libSegFault.so", RTLD_NOW | RTLD_LOCAL);
#endif

    if (debug_level >= Debug) {
        logfile_trace = output;
    } else {
        logfile_trace = &logfile_null;
    }

    if (debug_level >= Info) {
        logfile_info = output;
    } else {
        logfile_info = &logfile_null;
    }

    if (debug_level >= Warning) {
        logfile_warning = output;
    } else {
        logfile_warning = &logfile_null;
    }

    if (debug_level >= Error) {
        logfile_error = output;
    } else {
        logfile_error = &logfile_null;
    }

    signal(SIGHUP, reset_debug_signal_handler);
}

void reset_debug()
{
    setup_debug(debug_level, logfile_filename);
}

void reset_debug_signal_handler(int)
{
    reset_debug_needed = 1;
}

void reset_debug_if_needed()
{
    if( reset_debug_needed ) {
        reset_debug_needed = 0;
        reset_debug();
        if( const char* env = getenv( "ICECC_TEST_FLUSH_LOG_MARK" )) {
            ifstream markfile( env );
            string mark;
            getline( markfile, mark );
            if( !mark.empty()) {
                assert( logfile_trace != NULL );
                *logfile_trace << "flush log mark: " << mark << endl;
            }
        }
        if( const char* env = getenv( "ICECC_TEST_LOG_HEADER" )) {
            ifstream markfile( env );
            string header1, header2, header3;
            getline( markfile, header1 );
            getline( markfile, header2 );
            getline( markfile, header3 );
            if( !header1.empty()) {
                assert( logfile_trace != NULL );
                *logfile_trace << header1 << endl;
                *logfile_trace << header2 << endl;
                *logfile_trace << header3 << endl;
            }
        }
    }
}

void close_debug()
{
    if (logfile_null.is_open()) {
        logfile_null.close();
    }

    if (logfile_file.is_open()) {
        logfile_file.close();
    }

    logfile_trace = logfile_info = logfile_warning = logfile_error = nullptr;
}

/* Flushes all ostreams used for debug messages.  You need to call
   this before forking.  */
void flush_debug()
{
    if (logfile_null.is_open()) {
        logfile_null.flush();
    }

    if (logfile_file.is_open()) {
        logfile_file.flush();
    }
}

unsigned log_block::nesting;
