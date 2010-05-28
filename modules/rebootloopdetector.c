/**
   @file rebootloopdetector.c

   Detect reboots that are occurring too frequently and go to MALF.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

   @author Semi Malinen <semi.malinen@nokia.com>

   This file is part of Dsme.

   Dsme is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Dsme is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Dsme.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <dsme/protocol.h>
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>

#include <stdio.h>
#include <time.h>
#include <errno.h>

/*
 * minimum number of seconds since previous startup/reboot
 * for a startup to be considered normal
 */
#define DSME_MIN_REBOOT_TIME 120 // seconds

/* maximum number of unnormal reboots before going to MALF */
#define DSME_MAX_REBOOT_COUNT 10 // times

/* file to use for recording startup time & reboot count */
#define DSME_DEFAULT_STARTUP_INFO_FILE "/var/lib/dsme/startup_info"


// TODO: use env to make this module testable
static const char* startup_info_filename()
{
    return DSME_DEFAULT_STARTUP_INFO_FILE;
}

static const char* startup_info_tmpfilename()
{
    return DSME_DEFAULT_STARTUP_INFO_FILE ".tmp";
}

static bool read_startup_info(time_t* last_startup, unsigned* reboot_count)
{
    bool        read     = false;
    const char* filename = startup_info_filename();
    FILE* f;

    if ((f = fopen(filename, "r")) == 0) {
        dsme_log(LOG_DEBUG, "%s: %s", filename, strerror(errno));
    } else {
        if (fscanf(f, "%ld %d", last_startup, reboot_count) != 2) {
            dsme_log(LOG_DEBUG, "Error reading file %s", filename);
        } else {
            read = true;
        }

        (void)fclose(f);
    }

    return read;
}

static void write_startup_info(time_t startup_time, unsigned reboot_count)
{
    const char* tmpfilename = startup_info_tmpfilename();
    FILE*       f;

    if ((f = fopen(tmpfilename, "w+")) == 0) {
        dsme_log(LOG_DEBUG, "%s: %s", tmpfilename, strerror(errno));
    } else {
        bool temp_file_ok = true;

        if (fprintf(f, "%ld %d", startup_time, reboot_count) < 0) {
            temp_file_ok = false;
            dsme_log(LOG_DEBUG, "Error writing %s", tmpfilename);
        }

        if (fclose(f) != 0) {
            temp_file_ok = false;
            dsme_log(LOG_DEBUG, "%s: %s", tmpfilename, strerror(errno));
        }

        if (temp_file_ok) {
            const char* filename = startup_info_filename();

            if (rename(tmpfilename, filename) != 0) {
                dsme_log(LOG_DEBUG, "Error writing %s", filename);
            }
        }
    }
}

static bool is_in_reboot_loop()
{
    bool     reboot_loop_detected = false;
    time_t   now;
    time_t   last_startup;
    unsigned reboot_count;

    time(&now);

    if (read_startup_info(&last_startup, &reboot_count)) {
        unsigned long seconds = now - last_startup;

        if (seconds < DSME_MIN_REBOOT_TIME) {
            if (++reboot_count > DSME_MAX_REBOOT_COUNT) {

                reboot_loop_detected = true;

                dsme_log(LOG_DEBUG,
                         "%d reboots in a succession; reboot loop detected",
                         reboot_count);
            } else {
                dsme_log(LOG_DEBUG,
                         "%ld s since last reboot, reboot count is now %d",
                         seconds,
                         reboot_count);
            }
        } else {
            dsme_log(LOG_DEBUG,
                     "%d s or over since last reboot; resetting reboot count",
                     DSME_MIN_REBOOT_TIME);
            reboot_count = 0;
        }
    } else {
        reboot_count = 0;
    }

    write_startup_info(now, reboot_count);

    return reboot_loop_detected;
}

static void go_to_malf()
{
    // TODO: request MALF instead of SHUTDOWN
    DSM_MSGTYPE_SHUTDOWN_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);

    broadcast_internally(&req);
}

static void check_for_reboot_loop()
{
    if (is_in_reboot_loop()) {
        dsme_log(LOG_CRIT, "going to MALF due to a reboot loop");
        go_to_malf();
    }
}


void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "rebootloopdetector.so loaded");

  check_for_reboot_loop();
}

void module_fini(void)
{
  dsme_log(LOG_DEBUG, "rebootloopdetector.so unloaded");
}