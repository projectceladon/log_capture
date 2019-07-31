/* Copyright (C) Intel 2010
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
 * @file anruiwdt.c
 * @brief File containing functions for anr and uiwdt events processing.
 *
 * This file contains functions used to process ANR and UIWDT events.
 */

#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

#include <cutils/properties.h>

#ifdef CONFIG_BTDUMP
#include <libbtdump.h>
#endif

#include "crashutils.h"
#include "privconfig.h"
#include "anruiwdt.h"
#include "dropbox.h"
#include "fsutils.h"
#include "utils.h"

#define PATH_LENGTH			256
void do_copy_pvr(char * src, char * dest) {
   char buf[PATH_LENGTH] = {0, };
   FILE * fs = NULL;
   FILE * fd = NULL;
   fs = fopen(src,"r");
   fd = fopen(dest,"w");
   if (fs && fd) {
		while(fgets(buf, PATH_LENGTH, fs)) {
		    fputs(buf ,fd);
		 }
   }
   if (fs)
      fclose(fs);
   if (fd)
      fclose(fd);

   return;
}

#ifdef CONFIG_BTDUMP

struct bt_dump_arg {
    int eventtype;
    char *key;
    char *destion;
    char *eventname;
};

void bt_fork_run(const char *dest) {
    pid_t pid;
    int status;
    unsigned int loop_cnt = 1200; /* 2 mn */

    if ((pid = fork()) < 0) {
        LOGE("%s: Error while forking child\n", __FUNCTION__);
        return;
    } else if (!pid) {
        FILE *f_btdump;
        // make sure the child dies when crashlogd dies
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        f_btdump = fopen(dest, "w");
        bt_all(f_btdump);
        fclose(f_btdump);
        do_chown(dest, PERM_USER, PERM_GROUP);
        exit(0);
    }

    // parent process
    while(loop_cnt) {
        pid_t p = waitpid(pid, &status, WNOHANG);

        if (p == -1) {
            LOGE("%s: Error encountered while waiting for bt_pid: %d\n",
                 __FUNCTION__, pid);
            return;
        }

        if (p == pid && (WIFEXITED(status) || WIFSIGNALED(status)))
            return;

        usleep(100000);
        loop_cnt--;
    }

    kill(pid, SIGKILL);
    while(waitpid(pid, NULL, WNOHANG) >= 0){};

    return;
}

void *dump_bt_all(void *param) {
    struct bt_dump_arg *args = (struct bt_dump_arg *) param;
    char destion_btdump[PATHMAX];
    FILE *f_btdump;
    char cmd[512];
    static pthread_mutex_t run_once = PTHREAD_MUTEX_INITIALIZER;

    LOGV("Full process backtrace dump started");
    snprintf(destion_btdump, sizeof(destion_btdump), "%s/all_back_traces.txt",
             args->destion);

    if(pthread_mutex_trylock(&run_once)==0) {
        bt_fork_run(destion_btdump);
        pthread_mutex_unlock(&run_once);
    } else {
        f_btdump = fopen(destion_btdump, "w");
        if (f_btdump) {
            fprintf(f_btdump,"Another instance of bt_dump is running\n");
            fprintf(f_btdump,"Check previous ANR/UIWDT events\n");
            fclose(f_btdump);
            do_chown(destion_btdump, PERM_USER, PERM_GROUP);
        }
    }

    raise_event(args->key, CRASHEVENT, args->eventname, NULL, args->destion);
    LOGE("%-8s%-22s%-20s%s %s\n", CRASHEVENT, args->key, get_current_time_long(0),
         args->eventname, args->destion);

    if (!is_crashreport_available()) {
        LOGW("%s: Crashreport notification(CRASH_LOGS_COPY_FINISHED) skipped! Event id: %s.\n",
            __FUNCTION__, args->key);
    } else if (1
#ifdef FULL_REPORT
        && (args->eventtype != ANR_TYPE
        || start_dumpstate_srv(args->destion, args->key) <= 0)
#endif
        ) {
        /*done */
        snprintf(cmd, sizeof(cmd) - 1, "am broadcast -n com.intel.crashreport"
                 "/.specific.NotificationReceiver -a com.intel.crashreport.intent.CRASH_LOGS_COPY_FINISHED "
                 "-c android.intent.category.ALTERNATIVE --es com.intel.crashreport.extra.EVENT_ID %s",
                 args->key);

        int status = run_command(cmd, 30);
        if (status != 0)
            LOGI("%s: Notify crashreport status(%d) for command \"%s\".\n",
                 __FUNCTION__, status, cmd);
    }

    free(args->key);
    free(args->eventname);
    free(args->destion);
    free(param);
    return NULL;
}
#endif

/*
* Name          :
* Description   :
* Parameters    :
*/
int process_anruiwdt_event(struct watch_entry *entry, struct inotify_event *event) {
    char path[PATHMAX];
    char destion[PATHMAX];
    const char *dateshort = get_current_time_short(1);
    char *key = NULL;
    char *dir;
#ifdef CONFIG_BTDUMP
    struct bt_dump_arg *btd_param;
    pthread_t btd_thread;
    char bt_dis_prop[PROPERTY_VALUE_MAX];
#endif
    /* Check for duplicate dropbox event first */
    if ( manage_duplicate_dropbox_events(event) )
        return 1;

    key = generate_event_id(CRASHEVENT, entry->eventname);
    dir = generate_crashlog_dir(MODE_CRASH, key);
    if (dir != NULL) {
        snprintf(destion,sizeof(destion),"%s/%s", dir, "pvr_debug_dump.txt");
        do_copy_pvr("/d/pvr/debug_dump", destion);
        do_chown(destion, PERM_USER, PERM_GROUP);
        snprintf(destion,sizeof(destion),"%s/%s", dir, "fence_sync.txt");
        do_copy_pvr("/d/sync", destion);
        do_chown(destion, PERM_USER, PERM_GROUP);
    }
    snprintf(path, sizeof(path),"%s/%s", entry->eventpath, event->name);
    if (!dir || !file_exists(path)) {
        if (!dir)
            LOGE("%s: Cannot get a valid new crash directory...\n", __FUNCTION__);
        else {
            LOGE("%s: Cannot access %s\n", __FUNCTION__, path);
            free(dir);
        }
        raise_event(key, CRASHEVENT, entry->eventname, NULL, NULL);
        LOGE("%-8s%-22s%-20s%s\n", CRASHEVENT, key, get_current_time_long(0), entry->eventname);
        free(key);
        return -1;
    }

    snprintf(destion,sizeof(destion),"%s/%s", dir, event->name);
    do_copy_tail(path, destion, MAXFILESIZE);
    do_log_copy(entry->eventname, dir, dateshort, APLOG_TYPE);
    restart_profile_srv(1);

#ifdef CONFIG_DUMP_BINDER
    /*add binder transactions*/
    snprintf(destion,sizeof(destion),"%s/%s", dir, "binder_transactions.txt");
    do_copy_pvr(BINDER_TRANSACTIONS, destion);
    do_chown(destion, PERM_USER, PERM_GROUP);
    snprintf(destion,sizeof(destion),"%s/%s", dir, "binder_transaction_log.txt");
    do_copy_pvr(BINDER_TRANSACTION_LOG, destion);
    do_chown(destion, PERM_USER, PERM_GROUP);
    snprintf(destion,sizeof(destion),"%s/%s", dir, "binder_failed_transaction_log.txt");
    do_copy_pvr(BINDER_FAILED_TRANSACTION_LOG, destion);
    do_chown(destion, PERM_USER, PERM_GROUP);
#endif

#ifdef CONFIG_BTDUMP
    property_get(PROP_ANR_USERSTACK, bt_dis_prop, "0");
    if (!strcmp(bt_dis_prop, "0")) {
        /*alloc arguments */
        btd_param = malloc(sizeof(struct bt_dump_arg));
        btd_param->key = key;
        btd_param->destion = dir;
        btd_param->eventname = strdup(entry->eventname);
        btd_param->eventtype = entry->eventtype;

        if (pthread_create(&btd_thread, NULL, dump_bt_all, (void *) btd_param)) {
            LOGE("Cannot start full process list backtrace dump.");
        } else {
            /*Everything will end on the new thread */
            return 1;
        }
        free(btd_param->eventname);
        free(btd_param);
    }
#endif
    raise_event(key, CRASHEVENT, entry->eventname, NULL, dir);
    LOGE("%-8s%-22s%-20s%s %s\n", CRASHEVENT, key, get_current_time_long(0),
         entry->eventname, dir);
#ifdef FULL_REPORT
    start_dumpstate_srv(dir, key);
#endif
    free(key);
    free(dir);
    return 1;
}
