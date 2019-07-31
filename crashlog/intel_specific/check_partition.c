/* Copyright (C) Intel 2019
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
 * @file check_partition.c
 * @brief File containing functions for getting user space events.
 */

#include "crashutils.h"
#include "privconfig.h"
#include "fsutils.h"
#include "checksum.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef CONFIG_PARTITIONS_CHECK
int partition_notified = 1;

static const char * const mountpoints[] = {
    "/system",
    "/cache",
    "/config",
    "/logs",
    "/factory",
};

static int check_mounted_partition(const char *partition_name)
{
    unsigned int i;
    for (i=0; i < DIM(mountpoints); i++)
         if (!strncmp(partition_name, mountpoints[i], strlen(mountpoints[i])))
             return 1;
    return 0;
}

// TODO: make /logs mount check an option per CONFIG_LOGS_PATH
int check_mounted_partitions()
{
    FILE *fd;
    char mount_dev[256];
    char mount_dir[256];
    char mount_type[256];
    char mount_opts[256];
    char list_partition[256];
    int match;
    int mount_passno;
    int mount_freq;
    unsigned int nb_partition = 0;
    /* if an error is detected, output is set to 0 */
    int output = 1;

    if(!partition_notified)
        return output;

    memset(list_partition, '\0', sizeof(list_partition));
    fd = fopen("/proc/mounts", "r");
    if (fd == NULL) {
        LOGE("can not open mounts file \n");
        return output;
    }
    do {
        memset(mount_dev, '\0', sizeof(mount_dev));
        match = fscanf(fd, "%255s %255s %255s %255s %d %d\n",
                       mount_dev, mount_dir, mount_type,
                       mount_opts, &mount_freq, &mount_passno);
         if (strstr(mount_dev, "by-name") && check_mounted_partition(mount_dir)) {
             int max_append_size = sizeof(list_partition) - strlen(list_partition) - 1;
             nb_partition++;
             if (max_append_size > 0)
                 strncat(list_partition, mount_dir, max_append_size);
             if (((strncmp(mount_dir, "/logs", 5)) == 0) && strstr(mount_opts, "ro,"))
                  output = notify_partition_error(ERROR_LOGS_RO);
         }
    } while (match != EOF);

    if (nb_partition < DIM(mountpoints)) {
        if (!strstr(list_partition, "/logs"))
            output = notify_partition_error(ERROR_LOGS_MISSING);
        else
            output = notify_partition_error(ERROR_PARTITIONS_MISSING);
    }
    fclose(fd);

    if(!output)
        partition_notified = 0;

    return output;
}
#endif

#ifdef CONFIG_FACTORY_CHECKSUM
static char *checksum_ex_paths[] = {
    "/factory/userdata_footer",
    NULL
};

static void check_factory_checksum_callback(const char * file, mode_t st_mode) {
    char *reason = NULL;

    switch (st_mode & S_IFMT) {
        case S_IFBLK:  reason = "block device";     break;
        case S_IFCHR:  reason = "character device"; break;
        case S_IFDIR:  reason = "directory";        break;
        case S_IFIFO:  reason = "FIFO/pipe";        break;
        case S_IFLNK:  reason = "symlink";          break;
        case S_IFREG:  reason = "regular file";     break;
        case S_IFSOCK: reason = "socket";           break;
        default:       reason = "unknown";          break;
    }

    LOGE("%s: file skipped. encountered: %s for %s\n", __FUNCTION__, reason, file);
}

void check_factory_partition_checksum() {
    unsigned char checksum[CRASHLOG_CHECKSUM_SIZE];

    if (!directory_exists(FACTORY_PARTITION_DIR)) {
        LOGD("%s: Factory partition not present on current build. Skipping checksum verification\n", __FUNCTION__);
        return;
    }

    LOGD("%s: performing factory partition checksum calculation\n", __FUNCTION__);
    if (calculate_checksum_directory(FACTORY_PARTITION_DIR, checksum,
            check_factory_checksum_callback, (const char**)checksum_ex_paths) != 0) {
        LOGE("%s: failed to calculate factory partition checksum\n", __FUNCTION__);
        return;
    }

    if (file_exists(FACTORY_SUM_FILE)) {
        unsigned char old_checksum[CRASHLOG_CHECKSUM_SIZE+1];
        if (read_binary_file(FACTORY_SUM_FILE, old_checksum, CRASHLOG_CHECKSUM_SIZE) < 0) {
            LOGE("%s: failed in reading checksum from file: %s\n", __FUNCTION__, FACTORY_SUM_FILE);
        }

        if (memcmp(checksum, old_checksum, CRASHLOG_CHECKSUM_SIZE) != 0) {
            /* send event that something has changed */
            char *key = generate_event_id(INFOEVENT, "FACTORY_SUM");
            raise_event(key, INFOEVENT, "FACTORY_SUM", NULL, NULL);
            free(key);
            if (write_binary_file(FACTORY_SUM_FILE, checksum, CRASHLOG_CHECKSUM_SIZE) < 0) {
                LOGE("%s: failed in writing checksum to file: %s\n", __FUNCTION__, FACTORY_SUM_FILE);
                return;
            }
            LOGD("%s: %s file updated\n", __FUNCTION__, FACTORY_SUM_FILE);
        }
        else {
            LOGD("%s: no changes detected\n", __FUNCTION__);
        }
    }
    else {
        if (write_binary_file(FACTORY_SUM_FILE, checksum, CRASHLOG_CHECKSUM_SIZE) < 0) {
            LOGE("%s: failed in writing checksum to file: %s\n", __FUNCTION__, FACTORY_SUM_FILE);
            return;
        }
        LOGD("%s: %s file created\n", __FUNCTION__, FACTORY_SUM_FILE);
    }
}
#endif


