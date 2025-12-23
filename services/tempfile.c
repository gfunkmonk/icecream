/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


/* "More computing sins are committed in the name of
 * efficiency (without necessarily achieving it) than
 * for any other single reason - including blind
 * stupidity."  -- W.A. Wulf
 */



#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "tempfile.h"
#include "exitcode.h"

#ifndef _PATH_TMP
#define _PATH_TMP "/tmp"
#endif



/**
 * Create a file inside the temporary directory and register it for
 * later cleanup, and return its name.
 *
 * The file will be reopened later, possibly in a child.  But we know
 * that it exists with appropriately tight permissions.
 **/
int dcc_make_tmpnam(const char *prefix, const char *suffix, char **name_ret, int relative)
{
    char *template_name;
    char *final_name;
    size_t template_length;
    int fd;

    /* Build template: /tmp/prefix_XXXXXX */
    template_length = strlen(_PATH_TMP) + 1 + strlen(prefix) + 1 + 6 + 1;
    template_name = malloc(template_length);

    if (!template_name) {
        return EXIT_OUT_OF_MEMORY;
    }

    if (snprintf(template_name, template_length, "%s/%s_XXXXXX",
                 (relative ? &_PATH_TMP[1] : _PATH_TMP),
                 prefix) == -1) {
        free(template_name);
        return EXIT_OUT_OF_MEMORY;
    }

    /* Create the file atomically with mkstemp */
    fd = mkstemp(template_name);

    if (fd == -1) {
        free(template_name);
        return EXIT_IO_ERROR;
    }

    if (close(fd) == -1) {
        /* Clean up the created file */
        unlink(template_name);
        free(template_name);
        return EXIT_IO_ERROR;
    }

    /* If a suffix is needed, rename the file */
    if (suffix && suffix[0] != '\0') {
        size_t final_length = strlen(template_name) + strlen(suffix) + 1;
        final_name = malloc(final_length);

        if (!final_name) {
            unlink(template_name);
            free(template_name);
            return EXIT_OUT_OF_MEMORY;
        }

        if (snprintf(final_name, final_length, "%s%s", template_name, suffix) == -1) {
            unlink(template_name);
            free(template_name);
            free(final_name);
            return EXIT_OUT_OF_MEMORY;
        }

        if (rename(template_name, final_name) == -1) {
            unlink(template_name);
            free(template_name);
            free(final_name);
            return EXIT_IO_ERROR;
        }

        free(template_name);
        *name_ret = final_name;
    } else {
        *name_ret = template_name;
    }

    return 0;
}

int dcc_make_tmpdir(char **name_ret) {
    unsigned long tries = 0;
    char template[] = "icecc-XXXXXX";
    size_t tmpname_length = strlen(_PATH_TMP) + 1 + strlen(template) + 1;
    char *tmpname = malloc(tmpname_length);

    if (!tmpname) {
        return EXIT_OUT_OF_MEMORY;
    }

    if (snprintf(tmpname, tmpname_length, "%s/%s", _PATH_TMP, template) == -1) {
        free(tmpname);
        return EXIT_OUT_OF_MEMORY;
    }

    do {
        if (!mkdtemp(tmpname)) {
            if (++tries > 1000000) {
                free(tmpname);
                return EXIT_IO_ERROR;
            }

            switch (errno) {
            case EACCES:
            case EEXIST:
            case EISDIR:
            case ELOOP:
                continue;
            }

            free(tmpname);
            return EXIT_IO_ERROR;
        }

        break;
    } while (1);

    *name_ret = tmpname;

    return 0;
}
