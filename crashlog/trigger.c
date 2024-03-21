/* Copyright (C) Intel 2013
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file trigger.c
 * @brief File containing functions to process aplog events, bz events and stats
 * events.
 */

#include "inotify_handler.h"
#include "crashutils.h"
#include "fsutils.h"
#include "privconfig.h"
#include "utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>

#ifdef FULL_REPORT
static void compress_aplog_folder(char *folder_path)
{
    char cmd[PATHMAX];
    char spath[PATHMAX];
    DIR *d;
    struct dirent* de;

    /* Compress aplog/bplog files */
    snprintf(cmd, sizeof(cmd), "gzip %s/[ab]plog*", folder_path);
    run_command(cmd, 45);
    /* Change owner and group of those aplog files */
    d = opendir(folder_path);
    if (d == 0) {
         return;
    }
    else {
        while ((de = readdir(d)) != 0) {
           if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
               continue;
           snprintf(spath, sizeof(spath)-1,  "%s/%s", folder_path, de->d_name);
           do_chown(spath, PERM_USER, PERM_GROUP);
        }
        closedir(d);
    }
}
#else
static inline void compress_aplog_folder(char *folder_path __attribute__((__unused__))) {}
#endif

/**
* Name          : process_log_event
* Description   : This function manages treatment for aplog and bz triggers.
*                 In APLOG mode :
*                    - The number of packet represents the number of events and then 'crashlog' directories to be created.
*                    - The depth is the number of aplog to be copied in each created event 'crashlog' directorie.
*                 In BZ mode :
*                    - The number of copied aplog is equal to (nb of packet X depth)
*                 The depth value is first tried to be read from the trigger file ( in this case the number of packet is 1 by default).
*                 If reading failed or if file is empty, those values are read from properties.
* Parameters    :
*   char *rootdir     -> path of watched directory
*   char *triggername -> name of the file inside the watched directory that has triggered the event
*   mode              -> mode (BZ or APLOG)
 */
int process_log_event(char *rootdir, char *triggername, int mode) {
    char *key = NULL;
    char *dir = NULL;
    char path[PATHMAX];
    char destination[PATHMAX];
    char tmp[PATHMAX] = {'\0',};
    int nbPacket,aplogDepth = 0;
    int aplogIsPresent, DepthValueRead = 0, res = 0;
    int bplogFlag = 0;
    char value[PROPERTY_VALUE_MAX];
    const char *suppl_to_copy;
    char *event, *type;
    int packetidx, logidx, newdirperpacket, do_screenshot;
    unsigned int cnt_len = 1;

    switch (mode) {
        case MODE_BZ:
            newdirperpacket = 0;
            suppl_to_copy = "bz_description";
            do_screenshot = 1;
            event = BZEVENT;
            type = BZMANUAL;
            break;
        case MODE_APLOGS:
            newdirperpacket = 1;
            suppl_to_copy = NULL;
            do_screenshot = 0;
            event = APLOGEVENT;
            type = APLOGTRIG_EVNAME;
            break;
        default:
            LOGE("%s: Mode %d not supported, cannot process log event for %s!\n",
                    __FUNCTION__, mode, (triggername ? triggername : "no trigger file") );
            return -1;
            break;
    }
    if (rootdir && triggername)
    {
        snprintf(path, sizeof(path),"%s/%s", rootdir, triggername);
        if (file_exists(path)) {
            LOGI("Received trigger file %s for %s", path, ((mode == MODE_BZ) ? "BZ" : "APLOG" ));

            res = get_value_in_file(path,"APLOG=", tmp, sizeof(tmp));
            if (res < 0) {
                LOGE("%s : got error %s from function call \n",
                                    __FUNCTION__, strerror(-res));
            }

            if (!res && atoi(tmp) >= 0) {
                //if a value is specified inside trigger file, it overrides property value
                aplogDepth = atoi(tmp);
                nbPacket = 1;
                DepthValueRead = 1;
            }

            //Read bplog flag value specified inside trigger file
            memset(tmp, '\0', sizeof(tmp));

            res = get_value_in_file(path,"BPLOG=", tmp, sizeof(tmp));
            if (res < 0) {
                LOGE("%s : got %s - error %s from function call \n",
                        __FUNCTION__, path, strerror(-res));
            }
            if (!res) {
                bplogFlag = atoi(tmp);
            }
        }
    }
    /* When no value has been read from trigger file, fall back to values read from the properties */
    if (!DepthValueRead ) {
       /* Get Aplog depth (number of aplogs per packet) */
       property_get(PROP_APLOG_DEPTH, value, APLOG_DEPTH_DEF);
       aplogDepth = atoi(value);
       if (aplogDepth < 0)
           aplogDepth = 0;
       /* Get nb of packets */
       property_get(PROP_APLOG_NB_PACKET, value, APLOG_NB_PACKET_DEF);
       nbPacket = atoi(value);
       if (nbPacket < 0)
           nbPacket = 0;
       LOGD("%s: Trigger file not usable so get values from properties : Aplog Depth (%d) and Packet Nb (%d)", __FUNCTION__,
               aplogDepth, nbPacket);
    }
#ifndef CONFIG_APLOG
    /* Manage APLOG=0 which means bz type="enhancement"*/
    if ( aplogDepth != 0 )
        flush_aplog(APLOG, NULL, NULL, NULL);
#endif
    /* if we need more than one file*/
    if (aplogDepth && nbPacket) {
        property_get(PROP_APLOG_ROT_CNT, value, "1");
        /* logcat uses log10 to compute this out of an integer,
         * in our case, since the value is already string and strlen
         * gives the same output, use it*/
        cnt_len = strlen(value);
    }

    /* copy data file */
    for( packetidx = 0; packetidx < nbPacket ; packetidx++) {
        for(logidx = 0 ; logidx < aplogDepth ; logidx++) {
            if ( (packetidx == 0) && (logidx == 0) )
                snprintf(path, sizeof(path),"%s",APLOG_FILE_0);
            else
                snprintf(path, sizeof(path),"%s.%.*d",APLOG_FILE_0, cnt_len, (packetidx*aplogDepth)+logidx);

            //Check aplog file exists
            aplogIsPresent = file_exists(path);
            if( !aplogIsPresent ) break;

            if( ( newdirperpacket && (logidx == 0) ) || (!newdirperpacket && (packetidx == 0) && (logidx == 0) ) ) {
                key = generate_event_id(event, type);
                dir = generate_crashlog_dir(mode, key);
                if (!dir) {
                    LOGE("%s: Cannot get a valid new crash directory for %s...\n", __FUNCTION__,
                            (triggername ? triggername : "no trigger file"));
                    free(key);
                    return -1;
                }
            }

            if (dir != NULL) {
                /* Set destination file*/
                snprintf(destination,sizeof(destination),"%s/aplog.%.*d",
                        dir, cnt_len, (packetidx*aplogDepth)+logidx);

                do_copy_tail(path, destination, 0);
            }
        }
        /* When a new crashlog dir is created per packet, send an event per dir */
        if(newdirperpacket) {
            if (!dir)
                continue;

            snprintf(destination,sizeof(destination),"%s/user_comment", dir);
            snprintf(path, sizeof(path),"%s/%s",rootdir, triggername);
            do_copy_tail(path, destination, 0);

            compress_aplog_folder(dir);
            raise_event(key, event, type, NULL, dir);
            LOGE("%-8s%-22s%-20s%s %s\n", event, key, get_current_time_long(0), type, dir);
            free(key);
            free(dir);
            dir = NULL;
            if (rootdir)
                restart_profile_srv(2);
        }
    }
    /* When no new crashlog dir is created per packet, send an event only at the end */
    /* For bz_trigger, treats bz_trigger file content and logs one BZEVENT event in history_event */
    /* In case of bz_trigger with APLOG=0 which means bz type="enhancement" and so no logs needed. */
    if( !newdirperpacket ) {
        if (!dir) {
            key = generate_event_id(event, type);
            dir = generate_crashlog_dir(mode, key);
            if (!dir) {
                LOGE("%s: Cannot get a valid new crash directory for %s...\n", __FUNCTION__,
                        (triggername ? triggername : "no trigger file"));
                free(key);
                return -1;
             }
        }
        if (suppl_to_copy) {
            /* For BZ copy trigger file */
            snprintf(destination,sizeof(destination),"%s/%s",  dir, suppl_to_copy);
            snprintf(path, sizeof(path),"%s/%s", APLOG_DIR, BZTRIGGER);
            do_copy_tail(path,destination,0);

            /* In case of bz_trigger with BPLOG=1, copy bplog file(s) */
            if( bplogFlag == 1 ) {
                do_logs_copy(BPLOG_TYPE, -1, dir, "", MAXFILESIZE);
            }
        }

        snprintf(destination,sizeof(destination),"%s/user_comment", dir);
        snprintf(path, sizeof(path),"%s/%s",rootdir, triggername);
        do_copy_tail(path, destination, 0);

        if (do_screenshot) {
            do_screenshot_copy(path, dir);
        }
        compress_aplog_folder(dir);
        raise_event(key, event, type, NULL, dir);
        LOGE("%-8s%-22s%-20s%s %s\n", event, key, get_current_time_long(0), type, dir);
        free(key);
        free(dir);
        restart_profile_srv(2);
    }

#ifndef CONFIG_APLOG
    remove(APLOG_FILE_0);
#endif

    /*delete trigger file if necessary */
    if (rootdir && triggername) {
        snprintf(path, sizeof(path),"%s/%s",rootdir, triggername);
        remove(path);
    }
    return res;
}

int process_aplog_event(struct watch_entry *entry, struct inotify_event *event) {

    return process_log_event(entry->eventpath, event->name,
                             (strncmp(event->name, "bz", 2) == 0) ? MODE_BZ : MODE_APLOGS);
}

int process_stat_event(struct watch_entry *entry, struct inotify_event *event) {
    unsigned int j = 0;
    char type[20] = { '\0', };
    char path[PATHMAX];
    char destination[PATHMAX];
    char tmp_data_name[PATHMAX];
    const char *dateshort = get_current_time_short(1);
    char *key, *p, tmp[32];
    char *dir;

    snprintf(tmp, sizeof(tmp), "%s", event->name);
    /* change trigger into data in the event filename */
    p = strstr(tmp, "trigger");
    if ( p ){
        strcpy(p, "data");
    }

    key = generate_event_id(STATSEVENT, tmp);
    dir = generate_crashlog_dir(MODE_STATS, key);
    if (!dir) {
        LOGE("%s: Cannot get a valid new crash directory...\n", __FUNCTION__);
        raise_event(key, STATSEVENT, tmp, NULL, NULL);
        LOGE("%-8s%-22s%-20s%s\n", STATSEVENT, key, get_current_time_long(0), tmp);
        free(key);
        return -1;
    }

    /* copy data file */
    if ( p ) {
        find_matching_file(entry->eventpath, tmp, tmp_data_name);
        snprintf(path, sizeof(path), "%s/%s", entry->eventpath, tmp_data_name);
        snprintf(destination, sizeof(destination), "%s/%s", dir, tmp_data_name);
        do_copy(path, destination, MAXFILESIZE);
        if (remove(path) == -1) {
            LOGE("Failed to remove path %s\n", path);
        }
    }
    /*copy trigger file*/
    snprintf(path, sizeof(path),"%s/%s",entry->eventpath,event->name);
    snprintf(destination,sizeof(destination),"%s/%s", dir, event->name);
    do_copy(path, destination, MAXFILESIZE);
    if (remove(path) == -1) {
        LOGE("Failed to remove path %s\n", path);
    }
    /*create type */
    snprintf(tmp,sizeof(tmp),"%s",event->name);
    p = strstr(tmp,"_trigger");
    if (p) {
        for (j=0;j<sizeof(type)-1;j++) {
            if (p == (tmp+j))
                break;
            type[j]=toupper(tmp[j]);
        }
    } else
        snprintf(type,sizeof(type),"%s", event->name);
    /*for USBBOGUS case copy aplog file*/
    if (!strncmp(type, USBBOGUS, sizeof(USBBOGUS))) {
        usleep(TIMEOUT_VALUE);
        do_log_copy(type,dir,dateshort,APLOG_TYPE);
    }
    raise_event(key, STATSEVENT, type, NULL, dir);
    LOGE("%-8s%-22s%-20s%s %s\n", STATSEVENT, key, get_current_time_long(0), type, dir);
    free(key);
    free(dir);
    return 0;
}
