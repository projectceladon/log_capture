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
 * @file fsutils.c
 * @brief File containing functions for basic operations on files.
 *
 * This file contains the functions for every basic operations on files such as
 * reading, writing, copying, pattern search, deletion, mode change...etc.
 */

#include "fsutils.h"
#include "privconfig.h"
#include "crashutils.h"
#include "config_handler.h"
#include "tcs_wrapper.h"
#include "history.h"
#include "utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>
#include <ftw.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <libgen.h>
#include <regex.h>

long current_sd_size_limit = LONG_MAX;

/* used by get_path_size! */
static unsigned long long total_size = 0;

#define LOGGER_APLOG_PARAM "-b all -v threadtime -d -f"

#ifndef CONFIG_APLOG
static char *flush_aplog_cmd = NULL;
#endif
static char *flush_aplog_boot_cmd = NULL;

/* No header in bionic... */
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);

static int check_partlogfull(const char* path) {

    static int partlogfull_errorset = 0;

    /* path could either indicate LOGS_DIR or SDCARD_DIR
     * CRASHLOG_ERROR_FULL shall only be raised if path indicates LOGS_DIR
     */
    if (!partlogfull_errorset && !strncmp(path, LOGS_DIR, strlen(LOGS_DIR))) {
        partlogfull_errorset = 1;
        return 1;
    }

    return 0;
}

static int close_file(const char *filename, FILE * fd) {
    int res;

    errno = 0;
    res = fclose(fd);
    if (res != 0) {
        /* File closure could fail in some cases (full partition) */
        LOGE("%s: fclose on %s failed - error is %s\n", __FUNCTION__,
                filename, strerror(errno));
    }

    return res;
}

static int read_file(const char *filename, unsigned int *pcurrent) {
    FILE *fd;
    int res;

    /* Open file in reading */
    fd = fopen(filename, "r");
    if (fd != NULL) {
        /* Read current value from file */
        res = fscanf(fd, "%4u", pcurrent);
        if (res != 1) {
            /* Set it to 0 by default */
            *pcurrent = 0;
            LOGI("read KO res=%d, current=%d - error is %s\n", res,
                    *pcurrent, strerror(errno));
            res = 0;
        }
        /* Close file */
        res = close_file(filename, fd);
    } else if (errno == ENOENT) {
        LOGE("%s: File %s does not exist, fall back to folder 0.\n", __FUNCTION__, filename);
        /* Initialize file */
        reset_file(filename);
        *pcurrent = 0;
        res = 0;
    } else {
        LOGE("%s: Cannot open file %s - error is %s.\n", __FUNCTION__,
                filename, strerror(errno));
        raise_infoerror(ERROREVENT, CRASHLOG_ERROR_PATH);
        res = -1;
    }

    return res;
}

static int update_file(const char *filename, unsigned int current) {
    FILE *fd;
    int res;

    /* Open file in reading and writing */
    fd = fopen(filename, "r+");
    if (fd == NULL) {
        LOGE("%s: Cannot open the file %s in update mode\n", __FUNCTION__, filename);
        raise_infoerror(ERROREVENT, CRASHLOG_ERROR_PATH);
        return -1;
    }

    /* Write new current value in file */
    res = fprintf(fd, "%4u", ((current+1) % gmaxfiles));
    if (res <= 0) {
        LOGE("%s: Cannot update file %s - error is %s.\n", __FUNCTION__,
                filename, strerror(errno));
        /* Close file */
        close_file(filename, fd);
        return -1;
    }

    /* Close file */
    return (close_file(filename, fd));
}

static int calculate_size_of_path(const char __attribute__((__unused__)) *fpath,
                                   const struct stat *sb,
                                   int tflag,
                                   struct FTW __attribute__((__unused__)) *ftwbuf) {

    if (tflag == FTW_F && (sb->st_mode & S_IFMT) == S_IFREG)
        total_size += sb->st_size;

    return 0;
}

unsigned long long get_path_size(const char *path) {
    unsigned long long size = 0;
    int nopenfd = 20;
    /* mutex used to protect total_size */
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&lock);
    total_size = 0;

    if (nftw(path, calculate_size_of_path, nopenfd, 0) == -1) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    size = total_size;
    pthread_mutex_unlock(&lock);

    return size;
}

static int check_free_space_available(const char* path, unsigned int req) {
    struct statfs st;
    char prop[PROPERTY_VALUE_MAX];
    long available_portion;
    unsigned long long free_space, full_space, used_space, threshold, min_free_space, stop_space;

    if (statfs(path, &st) < 0) {
        LOGE("%s: warn: statfs failed on %s, err: %s!!!", __FUNCTION__, path, strerror(errno));
        return 0;
    }

    property_get(PROP_DATA_QUOTA, prop, "100");

    available_portion = strtol(prop, NULL, 10);
    available_portion = (available_portion > 100 || (available_portion < 0))
        ? 100 : available_portion;

    if (0 == available_portion) {
        LOGW("%s: warn: no space reserved for crash logging", __FUNCTION__);
        return -1;
    }

    free_space = st.f_bavail * st.f_bsize;
    full_space = st.f_blocks * st.f_bsize;
    used_space = get_path_size(path);
    threshold = 100 - req;

    min_free_space = full_space * req / 100;
    stop_space = full_space * available_portion * threshold / 10000;

    if (used_space >= stop_space || free_space < min_free_space) {
        LOGW("%s: quota reached. total space: %llu, total free space %llu, allocated space: %llu "
            "(limit %ld%%), minimum free space: %llu", __FUNCTION__, full_space, free_space,
            used_space, available_portion, min_free_space);
        return -1;
    }

    return 0;
}

void reset_file(const char *filename) {
    FILE *fd;

    fd = fopen(filename, "w");
    if (fd == NULL) {
        LOGE("%s: Cannot reset %s - %s\n", __FUNCTION__, filename,
            strerror(errno));
        return;
    }
    fprintf(fd, "%4u", 0);
    close_file(filename, fd);
    do_chown(filename, PERM_USER, PERM_GROUP);
}

int readline(int fd, char buffer[MAXLINESIZE]) {
    int size = 0, res;
    char *pbuffer = &buffer[0];

    /* Read the file until end of line or file */
    while ((res = read(fd, pbuffer, 1)) == 1 && size < MAXLINESIZE-1) {
        if (pbuffer[0] == '\n') {
            buffer[++size] = 0;
            return size;
        }
        pbuffer++;
        size++;
    }

    /* Check the last read result */
    if (res < 0) {
        /* ernno is checked in the upper layer as we could
           print the filename here */
        return res;
    }
    /* last line */
    buffer[size] = 0;
    return size;
}

int freadline(FILE *fd, char buffer[MAXLINESIZE]) {
    int size = 0, res;
    char *pbuffer = &buffer[0];

    /* Read the file until end of line or file */
    while ((res = fread(pbuffer, 1 , 1, fd)) == 1 && size < MAXLINESIZE-1) {
        if (pbuffer[0] == '\n') {
            buffer[++size] = 0;
            return size;
        }
        pbuffer++;
        size++;
    }

    /* Check the last read result */
    if (res < 0) {
        /* ernno is checked in the upper layer as we could
           print the filename here */
        return res;
    }
    /* last line */
    buffer[size] = 0;
    return size;
}

int count_lines_in_file(const char *filename) {
    char buffer[MAXLINESIZE];
    int fd, line_idx = 0;

    if (!filename)
        return -EINVAL;

    fd = open(filename, O_RDONLY);
    if(fd == -1) return -errno;

    while(readline(fd, buffer) > 0) {
        line_idx++;
    }
    close(fd);
    return line_idx;
}

int find_oneofstrings_in_file(const char *filename, const char *keywords[], int nbkeywords) {

    char buffer[MAXLINESIZE];
    int fd, linesize, idx;

    if (!keywords || !filename || !nbkeywords)
        return -EINVAL;

    fd = open(filename, O_RDONLY);
    if(fd == -1) return -errno;

    while((linesize = readline(fd, buffer)) > 0) {
        /* Remove the trailing '\n' if it's there */
        if (buffer[linesize-1] == '\n') {
            linesize--;
            buffer[linesize] = 0;
        }

        /* Check the keywords */
        for (idx = 0 ; idx < nbkeywords ; idx++) {
            if ( strstr(buffer, keywords[idx]) ) {
                close(fd);
                return 1;
            }
        }
    }
    close(fd);
    return 0;
}

int find_oneofstrings_in_file_with_keyword(char *filename, char **keywords, char *common_keyword,int nbkeywords){

    char buf1[MAXLINESIZE];
    char buf2[MAXLINESIZE] = {'\0'};
    char *buffer, *old_buffer, *tmp;
    int fd, linesize, idx;

    if (!keywords || !filename || !nbkeywords || !common_keyword)
        return -EINVAL;

    fd = open(filename, O_RDONLY);
    if(fd == -1)
        return -errno;

    buffer = &buf1[0];
    old_buffer = &buf2[0];
    while((linesize = readline(fd, buffer)) > 0) {
        /* Remove the trailing '\n' if it's there */
        if (buffer[linesize-1] == '\n') {
            linesize--;
            buffer[linesize] = 0;
        }

        /* Check the keywords */
        for (idx = 0 ; idx < nbkeywords ; idx++) {
            if ( strstr(buffer, keywords[idx]) ) {
                //and check the additional keyword including the previous line
                if ( strstr(buffer, common_keyword) || strstr(old_buffer, common_keyword) ) {
                    close(fd);
                    return 1;
                }
            }
        }
        //reverse pointer and store previous line this way
        tmp = old_buffer;
        old_buffer = buffer;
        buffer = tmp;
    }
    close(fd);
    return 0;
}

/**
 * Finds if file defined by filename contains given keyword and
 * if the matching line also ends with given tails (if provided).
 * It returns 1 if a match is found. 0 otherwise.
 * This function does not work on virtual files such as files located
 * in /proc directory.
 */
int find_str_in_standard_file(char *filename, char *keyword, char *tail) {
    char buffer[MAXLINESIZE];
    int fd, linesize;
    int taillen;

    if (keyword == NULL || filename == NULL)
        return -EINVAL;
    fd = open(filename, O_RDONLY);
    if(fd == -1)
        return -errno;

    /* Check the tail length once and for all */
    taillen = (tail ? strlen(tail) : 0);

    while((linesize = readline(fd, buffer)) > 0) {
        /* Remove the trailing '\n' if it's there */
        if (buffer[linesize-1] == '\n') {
            linesize--;
            buffer[linesize] = 0;
        }

        /* Check the keyword */
        if ( !strstr(buffer, keyword) ) continue;

        /* Check the tail? */
        if (!tail) break;

        /* a tail but the line is too short, continue... */
        if (taillen > linesize) continue;

        /* Do the tail's check*/
        if ( !strncmp(&buffer[linesize - taillen], tail, taillen) ) break;
    }

    close(fd);
    return (linesize > 0 ? 1 : 0);
}

/**
 * Finds if file defined by filename contains given keyword and
 * if the matching line also ends with given tails (if provided).
 * It returns 1 if a match is found. 0 otherwise.
 */
int find_str_in_file(const char *filename, const char *keyword, const char *tail)
{
    char buffer[4 * KB];
    int rc = 0;
    FILE *fd1;
    struct stat info;

    if (keyword == NULL || filename == NULL)
        return -EINVAL;

    if (stat(filename, &info) < 0) {
        LOGE("%s: can not open file: %s - error is %s.\n", __FUNCTION__, filename, strerror(errno) );
        return -errno;
    }
    fd1 = fopen(filename, "r");
    if(fd1 == NULL) {
        LOGE("%s : can not open file: %s - error is %s.\n", __FUNCTION__, filename, strerror(errno) );
        return -errno;
    }
    while(!feof(fd1)){
        if (fgets(buffer, sizeof(buffer), fd1) != NULL){
            /* Check the keyword */
            if (keyword && strstr(buffer,keyword)){
                /* Check the tail? */
                if (!tail){
                    rc = 1;
                    break;
                } else{
                    /* Do the tail's check*/
                    int buflen = strlen(buffer);
                    int str2len = strlen(tail);
                    if ((buflen > str2len) && (!strncmp(&(buffer[buflen-str2len-1]), tail, strlen(tail)))){
                        rc = 1;
                        break;
                    }
                }
            }
        }
    }
    fclose(fd1);
    return rc;
}

int get_value_in_file(char *file, char *keyword, char *value, unsigned int sizemax)
{
    char buffer[MAXLINESIZE];
    int keylen;
    FILE *fd;

    if ( !file || keyword == NULL || !value || !sizemax )
        return -EINVAL;

    if ( !file_exists(file) )
        return -ENOENT;

    if ((fd = fopen(file,"r")) == NULL)
        return -errno;

    /* Once and for all */
    keylen = strlen(keyword);

    while( !feof(fd) ){
        if (fgets(buffer, sizeof(buffer), fd) != NULL) {
            if ( strstr(buffer, keyword) ) {
                unsigned int buflen = strlen(buffer);
                if( buffer[buflen-1] == '\n')
                    buffer[--buflen]= '\0';
                int size = buflen - keylen;
                if (size > (int)sizemax) {
                    /* return bad argument...*/
                    LOGE("%s: %s found but buffer provided of %d bytes is too short to handle \"%s\"\n",
                        __FUNCTION__, keyword, sizemax, buffer);
                    fclose(fd);
                    return -EINVAL;
                }
                strncpy(value, &buffer[keylen], size);
                fclose(fd);
                return 0;
            }
        }
    }

    /* Did not find the keyword... */
    fclose(fd);
    return -1;
}

int get_sdcard_paths(e_dir_mode_t mode) {
    char value[PROPERTY_VALUE_MAX];
    DIR *d;

    CRASH_DIR = EMMC_CRASH_DIR;
    STATS_DIR = EMMC_STATS_DIR;
    APLOGS_DIR = EMMC_APLOGS_DIR;
    BZ_DIR = EMMC_BZ_DIR;

#ifndef FULL_REPORT
    (void)mode; /* unused */
    return 0;
#else

    property_get(PROP_CRASH_MODE, value, "");
    if ((!strncmp(value, "lowmemory", 9)) || (mode == MODE_CRASH_NOSD) || !sdcard_allowed())
        return 0;
    /* check whether there's extra available space for new logs in the SD card */
    if (check_free_space_available(SDCARD_LOGS_DIR, SDCARD_MINIMUM_FREESPACE_PERCENT) < 0) {
        return 0;
    }

    errno = 0;
    if (!file_exists(SDCARD_LOGS_DIR)) {
        int ret = mkdir(SDCARD_LOGS_DIR, 0777);
        if (ret < 0)
            return -errno;
    }
    if ( (d = opendir(SDCARD_LOGS_DIR)) != NULL ){
        CRASH_DIR = SDCARD_CRASH_DIR;
        STATS_DIR = SDCARD_STATS_DIR;
        APLOGS_DIR = SDCARD_APLOGS_DIR;
        BZ_DIR = SDCARD_BZ_DIR;
        closedir(d);
    }
    return -errno;
#endif
}

int rmfr_match(const char *match) {
    int count;
    struct dirent **filelist;
    char *name;
    char *path;
    char *filename;
    char found[PATHMAX];

    if (!match)
        return -EINVAL;

    filename = strrchr (match, '/');
    if (filename == NULL)
        return -EINVAL;

    path = strndup(match, strlen(match) - strlen(filename));
    if (!path)
        return -EFAULT;

    filename++;

    count = scandir(path, &filelist, NULL, NULL);
    if (count < 0) {
        free(path);
        return -errno;
    }

    while (count--) {
        name = filelist[count]->d_name;
        if (!name) {
            free(path);
            return -EFAULT;
        }

        if (strstr(name, filename)) {
            snprintf(found, sizeof(found), "%s/%s", path, name);
            rmfr(found);
        }
        free(filelist[count]);
    }

    free(filelist);
    free(path);

    return 0;
}

static char *reserve_crash_folder(e_dir_mode_t mode, unsigned int *current) {
    char path[PATHMAX];
    int res;
    char *dir = NULL;

    get_sdcard_paths(mode);

    switch(mode) {
        case MODE_CRASH:
        case MODE_CRASH_NOSD:
            snprintf(path, sizeof(path), CRASH_CURRENT_LOG);
            dir = CRASH_DIR;
            break;
        case MODE_APLOGS:
            snprintf(path, sizeof(path), APLOGS_CURRENT_LOG);
            dir = APLOGS_DIR;
            break;
        case MODE_BZ:
            snprintf(path, sizeof(path), BZ_CURRENT_LOG);
            dir = BZ_DIR;
            break;
        case MODE_STATS:
            snprintf(path, sizeof(path), STATS_CURRENT_LOG);
            dir = STATS_DIR;
            break;
        case MODE_KDUMP:
            snprintf(path, sizeof(path), CRASH_CURRENT_LOG);
            dir = KDUMP_CRASH_DIR;
            break;
        default:
            LOGE("%s: Invalid mode %d\n", __FUNCTION__, mode);
            return NULL;
    }

    /* check whether there's extra available space for new logs */
    if (!strncmp(dir, LOGS_DIR, strlen(LOGS_DIR))) {
        if( check_free_space_available(LOGS_DIR, LOGSDIR_MINIMUM_FREESPACE_PERCENT) < 0) {
            return NULL;
        }
    }

    /* Read current value in file */
    res = read_file(path, current);
    if (res < 0)
        return NULL;

    /* Open file in read/write mode to update the new current */
    res = update_file(path, *current);
    if (res < 0)
        return NULL;


    return dir;
}

int create_empty_file(char* filename) {
    FILE *fp;
    fp = fopen(filename, "w");
    if (fp == NULL) {
        LOGE("can not create empty file: %s\n", filename);
        return -errno;
    }
    fclose(fp);
    do_chown(filename, PERM_USER, PERM_GROUP);

    return 0;
}

static int create_crash_folder(char *path) {
    /* Create a fresh directory */
    if (mkdir(path, 0777) == -1) {
        LOGE("%s: Cannot create dir %s\n", __FUNCTION__, path);
        /* Full partition (no space left on device)
         * path could either indicate LOGS_DIR or SDCARD_DIR
         * CRASHLOG_ERROR_FULL shall only be raised if path indicates LOGS_DIR
         */
        if ((errno == ENOSPC) && check_partlogfull(path)) {
            raise_infoerror(ERROREVENT, CRASHLOG_ERROR_FULL);
        } else {
            /* Error */
            raise_infoerror(ERROREVENT, CRASHLOG_ERROR_PATH);
        }
        return -1;
    }

    if (!strstr(path, "sdcard"))
        do_chown(path, PERM_USER, PERM_GROUP);

    return 0;
}

char *generate_crashlog_dir(e_dir_mode_t mode, char *unique) {
    char path[PATHMAX];
    unsigned int current;
    char *dir;

    dir = reserve_crash_folder(mode, &current);
    if (!dir)
        return NULL;

    snprintf(path, sizeof(path), "%s%d_", dir, current);
    rmfr_match(path);
    strncat(path, unique, (strlen(path)+strlen(unique)>=sizeof(path)) ?
            (sizeof(path) - strlen(path)) : strlen(unique));

    /* Check if directory is already in history, and delete path */
    if (directory_exists(path)) {
        history_delete_first_existent_logcrashpath(path);
    }

    if (create_crash_folder(path) < 0)
        return NULL;

    return strdup(path);
}

int find_new_crashlog_dir(e_dir_mode_t mode) {
    char path[PATHMAX];
    unsigned int current;
    char *dir;

    dir = reserve_crash_folder(mode, &current);
    if (!dir)
        return -1;

    snprintf(path, sizeof(path), "%s%d", dir, current);

    /* Check if directory is already in history, and delete path */
    if (directory_exists(path)) {
        history_delete_first_existent_logcrashpath(path);
    }

    /* Call rmfr which will fail if the path does not exist
     * but doesn't matter as we create it afterwards
     */
    rmfr(path);

    if (create_crash_folder(path) < 0)
        return -1;

    return current;
}

int find_matching_file(const char *dir_to_search, const char *pattern, char *filename_found)
{
    DIR *d;
    struct dirent* de;
    int result = 0;

    if (dir_to_search == NULL) return -EINVAL;
    if (pattern == NULL) return -EINVAL;
    if (filename_found == NULL) return -EINVAL;

    d = opendir(dir_to_search);
    if (d == NULL) return -errno;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (strstr(name, pattern)){
            sprintf(filename_found, "%s", name);
            result = 1;
            break;
        }
    }
    closedir(d);
    return result;
}

int rmfr(char *path) {
    return rmfr_specific(path, 1, 1);
}

int rmfr_specific(char *path, int remove_entry, int remove_subdirs) {
    DIR *d;
    struct dirent *de;
    char fsentry[PATHMAX];
    int subres = 0;
    unsigned char isFile = 0x8;
    errno = 0;

    remove_subdirs = (remove_entry != 0) ? remove_entry : remove_subdirs;

    /* Check for a simple file or link first */
    if ( !unlink(path) ) return 0;

    /* Failed; if the error was not EISDIR or ENOENT,
     * no need to pursue...
     */
    if ( errno != EISDIR && errno != ENOENT ) return -1;

    /* path is a directory, remove all the contents recursively
     * before deleting it
     */
    d = opendir(path);
    if ( d == NULL ) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        /* Remove file in every case and remove directory if required */
        if ( de->d_type == isFile || (de->d_type != isFile && remove_subdirs) ) {
            snprintf(fsentry, sizeof(fsentry), "%s/%s", path, de->d_name);
            if ( unlink(path) ) {
                if (errno == EISDIR)
                    subres = rmfr_specific(fsentry, 1, 1);
                if (subres) return -1;
            }
        }
    }
    closedir(d);
    if (remove_entry)
        /* Finally delete the empty directory */
        return rmdir(path);

    return 0;
}

void reset_log_data(const char *path, const char *rule) {
    char full_path[PATHMAX];
    regex_t re;
    DIR *d;
    struct dirent* de = NULL;

    if (path == NULL || rule == NULL) return;

    if (regcomp(&re, rule, REG_EXTENDED) != 0) {
        LOGE("Could not compile regular expression: %s", rule);
        return;
    }

    d = opendir(path);
    if (d == NULL) {
        regfree(&re);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        int status = regexec(&re, de->d_name, 0, NULL, 0);
        if (!status) {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, de->d_name);
            LOGD("cleaning %s", full_path);
            rmfr(full_path);
        }
    }
    regfree(&re);
    closedir(d);
}

mode_t get_mode(const char *s)
{
    mode_t mode = 0;
    while (*s) {
        if (*s >= '0' && *s <= '7') {
            mode = (mode << 3) | (*s - '0');
        } else {
            return -1;
        }
        s++;
    }
    return mode;
}

int do_chmod(char *path, char *mode)
{
#ifdef USE_SYSTEM_CMDS
    char *cmd[PATHMAX + 20];
    cmd[0] = 0;
    snprintf(cmd, sizeof(cmd)-1, "/system/bin/chmod %s %s", mode, path);
    if (cmd[0] == 0 || run_command(cmd, 15) == -1 ) {
            return errno;
    }
    return 0;
#else
    mode_t mod = get_mode(mode);
    if (chmod(path, mod) < 0) {
        return errno;
    }
    return 0;
#endif
}

#ifndef TEST_USER
static unsigned int android_name_to_id(const char *name, int *res)
{

    const struct crashlogd_id_info *info = crashlogd_ids;
    unsigned int n;
    *res = 0;

    for (n = 0; n < sizeof(crashlogd_ids)/sizeof(crashlogd_ids[0]); n++) {
        if (!strcmp(info[n].name, name))
            return info[n].aid;
    }

    *res = EINVAL;
    return -1U;
}
#endif

static unsigned int decode_uid(const char *s, int *res)
{
    *res = 0;

    if (!s || *s == '\0') {
        *res = -EINVAL;
        return -1;
    }
#ifndef TEST_USER
    unsigned int v;
    if (isalpha(s[0]))
        return android_name_to_id(s, res);

    errno = 0;
    v = (unsigned int)strtoul(s, 0, 0);
    if (errno) {
        *res = -errno;
        return -1U;
    }
    return v;
#else
    /* HACK for testing only */
    return TEST_USER;
#endif
}

int do_chown(const char *file, char *uid, char *gid)
{
    unsigned int duid, dgid;
    int result = 0;

    if (file == NULL) return -ENOENT;

    if (strstr(file, SDCARD_CRASH_DIR))
        return 0;

    duid = decode_uid(uid, &result);
    if ( result ) return result;

    dgid = decode_uid(gid, &result);
    if ( result ) return result;

    if ( chown(file, duid, dgid) )
        return -errno;

    return 0;
}

ssize_t do_read(int fd, void *buf, size_t len)
{
    ssize_t nr;
    while (1) {
        nr = read(fd, buf, len);
        if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
            continue;
        return nr;
    }
}

ssize_t do_write(int fd, const void *buf, size_t len)
{
    ssize_t nr;
    while (1) {
        nr = write(fd, buf, len);
        if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
            continue;
        else if (nr < 0)
            return -errno;
        return nr;
    }
}

int do_copy_eof(const char *src, const char *des)
{
    char buffer[CPBUFFERSIZE];
    int rc = 0;
    int fd1 = -1, fd2 = -1;
    struct stat info;
    int r_count, w_count = 0;

    if (src == NULL || des == NULL) return -EINVAL;

    if (stat(src, &info) < 0) {
        LOGE("%s: can not open file: %s\n", __FUNCTION__, src);
        return -errno;
    }

    if ( ( fd1 = open(src, O_RDONLY) ) < 0 ) {
        return -errno;
    }

    if ( ( fd2 = open(des, O_WRONLY | O_CREAT | O_TRUNC, 0660) ) < 0) {
        LOGE("%s: can not open file: %s\n", __FUNCTION__, des);
        close(fd1);
        return -errno;
    }

    /* Start copy loop */
    while (1) {
        /* Read data from src */
        r_count = do_read(fd1, buffer, CPBUFFERSIZE);
        if (r_count < 0) {
            LOGE("%s: read failed, err:%s", __FUNCTION__, strerror(errno));
            rc = -1;
            break;
        }

        if (r_count == 0)
            break;

        /* Copy data to des */
        w_count = do_write(fd2, buffer, r_count);
        if (w_count < 0) {
            LOGE("%s: write failed, err:%s", __FUNCTION__, strerror(errno));
            rc = -1;
            break;
        }
        if (r_count != w_count) {
            LOGE("%s: write failed, r_count:%d w_count:%d",
                 __FUNCTION__, r_count, w_count);
            rc = -1;
            break;
        }
    }

    /* Error occurred while copying data */
    if ((rc == -1) && (w_count == -ENOSPC)) {
        /* CRASHLOG_ERROR_FULL shall only be raised if des indicates LOGS_DIR */
        if (check_partlogfull(des))
            raise_infoerror(ERROREVENT, CRASHLOG_ERROR_FULL);
    }

    if (fd1 >= 0)
        close(fd1);
    if (fd2 >= 0)
        close(fd2);

    do_chown(des, PERM_USER, PERM_GROUP);
    return rc;
}

int do_copy_eof_dir(const char *srcdir, const char *dstdir)
{
    int ret = 0;
    DIR *d;
    struct dirent *de;
    char src_path[PATHMAX] = {'\0'};
    char dst_path[PATHMAX] = {'\0'};

    if (!srcdir || !dstdir)
        return -EINVAL;

    d = opendir(srcdir);
    if (!d) {
        LOGE("%s: failed to open src dir %s : %s (%d)\n",
             __FUNCTION__, srcdir, strerror(errno), errno);
        return -errno;
    }

    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") ||
            de->d_type != DT_REG)
            continue;

        snprintf(src_path, sizeof(src_path), "%s/%s", srcdir, de->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dstdir, de->d_name);

        ret = do_copy_eof(src_path, dst_path);
        if (ret) {
            LOGE("%s: failed to copy %s in %s\n",
                 __FUNCTION__, src_path, dst_path);
            break;
        }
    }

    closedir(d);

    return ret;
}

int do_copy_utf16_to_utf8(const char *src, const char *des)
{
    char  buffer16[CPBUFFERSIZE];
    char  buffer8[CPBUFFERSIZE];
    int rc = 0;
    int fd1 = -1, fd2 = -1;
    struct stat info;
    int r_count, w_count = 0, buf8_count, buf16_count;

    if (src == NULL || des == NULL) return -1;

    if (stat(src, &info) < 0) {
        LOGE("%s: can not open file: %s\n", __FUNCTION__, src);
        return -1;
    }

    if ( ( fd1 = open(src, O_RDONLY) ) < 0 ) {
        return -1;
    }

    if ( ( fd2 = open(des, O_WRONLY | O_CREAT | O_TRUNC, 0660) ) < 0) {
        LOGE("%s: can not open file: %s\n", __FUNCTION__, des);
        close(fd1);
        return -1;
    }

    /* Start copy loop */
    while (1) {
        /* Read data from src */
        r_count = do_read(fd1, buffer16, CPBUFFERSIZE);
        if (r_count < 0) {
            LOGE("%s: read failed, err:%s", __FUNCTION__, strerror(errno));
            rc = -1;
            break;
        }

        if (r_count == 0)
            break;
        /* convert loop, basic strategy => remove odd char */
        for (buf8_count = 0, buf16_count = 0; buf16_count < r_count; buf8_count++, buf16_count += 2) {
            buffer8[buf8_count] = (char) buffer16[buf16_count];
        }

        /* Copy data to des */
        w_count = do_write(fd2, buffer8, buf8_count);
        if (w_count < 0) {
            LOGE("%s: write failed, err:%s", __FUNCTION__, strerror(errno));
            rc = -1;
            break;
        }
        if (buf8_count != w_count) {
            LOGE("%s: write failed, r_count:%d w_count:%d",
                 __FUNCTION__, r_count, w_count);
            rc = -1;
            break;
        }
    }

    /* Error occurred while copying data */
    if ((rc == -1) && (w_count == -ENOSPC)) {
        /* CRASHLOG_ERROR_FULL shall only be raised if des indicates LOGS_DIR */
        if (check_partlogfull(des))
            raise_infoerror(ERROREVENT, CRASHLOG_ERROR_FULL);
    }

    if (fd1 >= 0)
        close(fd1);
    if (fd2 >= 0)
        close(fd2);

    do_chown(des, PERM_USER, PERM_GROUP);
    return rc;
}

int do_copy_tail(char *src, char *dest, int limit) {
    int rc = 0;
    int fsrc = -1, fdest = -1;
    struct stat info;
    off_t offset = 0;

    if (src == NULL || dest == NULL) return -EINVAL;

    if (stat(src, &info) < 0) {
        return -errno;
    }

    if ( ( fsrc = open(src, O_RDONLY) ) < 0 ) {
        return -errno;
    }

    if ( ( fdest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0660) ) < 0) {
        close(fsrc);
        return -errno;
    }

    if ((limit == 0) || (info.st_size < limit))
        limit = info.st_size;

    if (info.st_size > limit)
        offset = info.st_size - limit;

    rc = sendfile(fdest, fsrc, &offset, limit);
    /* CRASHLOG_ERROR_FULL shall only be raised if dest indicates LOGS_DIR */
    if ((rc != -1) && (rc != limit) && check_partlogfull(dest))
        raise_infoerror(ERROREVENT, CRASHLOG_ERROR_FULL);

    close(fsrc);
    close(fdest);
    do_chown(dest, PERM_USER, PERM_GROUP);
    return rc;
}

int do_copy(char *src, char *dest, int limit) {
    int rc = 0;
    int fsrc = -1, fdest = -1;
    struct stat info;

    if (src == NULL || dest == NULL) return -EINVAL;

    if (stat(src, &info) < 0) {
        return -errno;
    }

    if ( ( fsrc = open(src, O_RDONLY) ) < 0 ) {
        return -errno;
    }

    if ( ( fdest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0660) ) < 0) {
        close(fsrc);
        return -errno;
    }

    if (limit == 0) {
        /* test this late as if limit is 0, the dest file shall be
         * empty
         */
        close(fsrc);
        close(fdest);
        do_chown(dest, PERM_USER, PERM_GROUP);
        return 0;
    }

    if (info.st_size <= limit)
        limit = info.st_size;

    rc = sendfile(fdest, fsrc, NULL, limit);
    /* CRASHLOG_ERROR_FULL shall only be raised if dest indicates LOGS_DIR */
    if ((rc != -1) && (rc != limit) && check_partlogfull(dest))
        raise_infoerror(ERROREVENT, CRASHLOG_ERROR_FULL);

    close(fsrc);
    close(fdest);
    do_chown(dest, PERM_USER, PERM_GROUP);
    return rc;
}


/**
 * Name          : do_copy_circular
 * Description   : copy from source file to destination directory, while looping through backup
 *                 files until a given limit is met, or there are no more backup files available.
 *                 start_index could indicate that the first file to be copied should be a backup
 *                 file. Pattern of the backup files matched is: file.index.extension, while for
 *                 the first file it is: `file`, if available, or `file.extension`, otherwise.
 *                 returns the status of the copy process.
 *
 * @param source - full path to the base file
 * @param destination - destination directory where the files should be copied
 * @param extension - extension for the files copied
 * @param extra - additional string to be appended to the destination files
 * @param limit - overall maximum size in bytes to be copied
 * @param start_index - indicates the file from which we should start(0, base, otherwise backup
 *                      file index)
 * @param cnt_len - counter index len (for cases in which the rotation counter has a fixed
 *                  length eg. 01, 02 .. 99)
 */
static int do_copy_circular(const char *source, const char *destination, const char *extension,
        const char *extra, off_t limit, unsigned int start_index, unsigned int cnt_len) {
    char path[PATHMAX] = {'\0'};
    char dest[PATHMAX] = {'\0'};
    int len_base_path, len_dest_path;
    int index = start_index, rc = 0;
    const char *file = strrchr(source,'/') + 1;
    path[PATHMAX - 1] = '\0';

    if (!limit || !file)
        return 0;

    snprintf(path, PATHMAX, "%s", source);
    snprintf(dest, PATHMAX, "%s/%s", destination, file);
    len_base_path = strlen(path);
    len_dest_path = strlen(dest);

    if (!index) {
        snprintf(dest + len_dest_path, PATHMAX - len_dest_path, "%s%s", extra, extension);
        if (!file_exists(source))
            snprintf(path + len_base_path, PATHMAX - len_base_path, "%s", extension);

        rc = do_copy_tail(path, dest, limit);
        if (rc < 0)
            return rc;

        limit -= rc;
        index++;
    }

    while (limit) {
        snprintf(dest + len_dest_path, PATHMAX - len_dest_path, "%s.%.*d%s", extra, cnt_len, index, extension);
        snprintf(path + len_base_path, PATHMAX - len_base_path, ".%.*d%s", cnt_len, index, extension);
        rc = do_copy_tail(path, dest, limit);
        if (rc < 0)
            return rc;

        limit -= rc;
        index++;
    }

    return rc;
}

int do_mv(char *src, char *dest) {
    struct stat info;

    if (src == NULL || dest == NULL) return -EINVAL;

    if (stat(src, &info) < 0) {
        return -errno;
    }
    /* check if destination exists */
    if (stat(dest, &info)) {
        /* an error, unless the destination was missing */
        if (errno != ENOENT) {
            LOGE("%s: failed on '%s', err:%s", __FUNCTION__, dest, strerror(errno));
            return -1;
        }
    }
    /* attempt to move it */
    if (rename(src, dest)) {
        LOGE("%s: failed on '%s', err:%s\n",  __FUNCTION__, src, strerror(errno));
        return -1;
    }
    return 0;
}

int do_mv_in_dir(char *src, char *dest_dir) {
    int status = -EINVAL;
    char destination[PATHMAX];
    char *basename;

    if (src == NULL || dest_dir == NULL || strlen(src) == 0)
        return status;

    basename = strrchr(src, '/');
    if (!basename)
        return status;

    snprintf(destination, sizeof(destination),"%s/%s", dest_dir, basename);

    return do_mv(src, destination);
}

void qs_swap(void *array, int *indexes, int first, int second) {
    int itmp;
    char *vtmp;
    char **carray = array;

    itmp = indexes[first];
    indexes[first] = indexes[second];
    indexes[second] = itmp;

    vtmp = carray[first];
    carray[first] = carray[second];
    carray[second] = vtmp;
}

int qs_partition(void *array, int *indexes, int start, int end, int pivot) {
    int j = start, i;

    qs_swap(array, indexes, pivot, end);
    for (i = start ; i < end ; i++) {
        if (indexes[i] <= indexes[end]) {
            qs_swap(array, indexes, i, j);
            j++;
        }
    }
    qs_swap(array, indexes, end, j);
    return j;
}

void __quicksort(void *array, int *indexes, int start, int end) {
    int pivot;
    if ( start < end ) {
        pivot = qs_partition(array, indexes, start, end, start);
        __quicksort(array, indexes, start, pivot-1);
        __quicksort(array, indexes, pivot+1, end);
    }
}

#ifdef __DEBUG_QS__
void dump_char_array(char **array, int size) {
    int idx;

    for (idx = 0 ; idx < size ; idx++)
        printf("line %d: %s", idx, array[idx]);
}

void dump_int_array(int *array, int size) {
    int idx;

    printf("[");
    for (idx = 0 ; idx < size - 1 ; idx++)
        printf("%d, ", array[idx]);
    printf("%d]\n", array[idx]);
}
#endif

int quicksort(void *array, int dim, int pivot) {
    int *indexes, i, diff;

    indexes = (int*)malloc(dim*sizeof(int));
    if (indexes == NULL) return -ENOMEM;

    diff = dim - pivot;
    for (i = 0 ; i < dim ; i++) {
        if (i < pivot)
            indexes[i] = diff + i;
        else indexes[i] = i - pivot;
    }
#ifdef __DEBUG_QS__
    printf("Sort an array of %d items from pivot %d containing:\n", dim, pivot);
    dump_char_array((char**)array, dim);
    printf("Unsorted indexes: ");
    dump_int_array(indexes, dim);
#endif
    __quicksort(array, indexes, 0, dim-1);

#ifdef __DEBUG_QS__
    printf("Sorted into:\n");
    dump_char_array((char**)array, dim);
    printf("Sorted indexes: ");
    dump_int_array(indexes, dim);
#endif

    free(indexes);
    return 0;
}

/*
* Name          : cache_file
* Description   : copy source file lines into buffer.
*                 returns a negative value on error or the number of lines read and copied into the buffer.
*                 offset value is used to skip the file's first lines.
*/
int cache_file(char *filename, char **records, int maxrecords, int cachemode, int offset) {

    char curline[MAXLINESIZE];
    int fd, res = 0, index, line_idx = 0;

    if (cachemode != CACHE_START && cachemode != CACHE_TAIL) {
        return -EINVAL;
    }
    if ( !filename || !records || (offset < 0) || ((offset >= maxrecords) && (cachemode == CACHE_START))) {
        return -EINVAL;
    }
    if (maxrecords == 0) return 0;

    if ( ( fd = open(filename, O_RDONLY) ) < 0) {
        return -errno;
    }
    /* Initialize the buffer to NULL pointers */
    for ( index = 0 ; index < maxrecords ; index++)
        records[index] = NULL;

    if (cachemode == CACHE_START) {
        for (index = 0 ; index < maxrecords ; index++) {
            if ( (res = readline(fd, curline)) < 0) {
                /* readline failed, cleanup and exit */
                goto do_cleanup;
            }
            /*Start to copy line in the buffer when line number is equal to offset value*/
            if ( line_idx >= offset) {
                if (res == 0) {
                    /* file terminated */
                    close(fd);
                    return (index - offset);
                }
                /* add a new line to our buffer */
                records[index - offset] = strdup(curline);
                if (records[index - offset] == NULL) {
                    res = -errno;
                    goto do_cleanup;
                }
            }
            line_idx++;
        }
        close(fd);
        return index;
    }

    if (cachemode == CACHE_TAIL) {
        int curindex = 0, count = 0;
        while((res = readline(fd, curline)) > 0) {
            /*Start to copy line when line number is equal to offset value*/
            if ( line_idx >= offset) {
                /* add a new file to our buffer */
                if (records[curindex] != NULL)
                    free(records[curindex]);
                records[curindex] = strdup(curline);
                if (records[curindex] == NULL) {
                    res = -errno;
                    goto do_cleanup;
                }
                curindex = (curindex + 1) % maxrecords;
                if (count < maxrecords)
                    count++;
            }
            line_idx++;
        }
        if ( res < 0) {
            /* readline failed, cleanup and exit */
            goto do_cleanup;
        }
        /* res == 0 => EOF */
        if (count == maxrecords && curindex != 0) {
            /* Reordering the buffer is necessary */
#ifdef __DEBUG_QS__
            printf("Reorder the buffer (count = %d, maxrecords = %d, curindex = %d)\n", count, maxrecords, curindex);
#endif
            if ((res = quicksort(records, count, curindex)) < 0) {
                LOGE("%s: Cannot sort the final array: %s\n", __FUNCTION__, strerror(-res));
                goto do_cleanup;
            }
        }
        close(fd);
        return count;
    }
do_cleanup:
    for ( index = 0 ; index < maxrecords ; index++)
        if (records[index] != NULL) {
            free(records[index]);
            records[index] = NULL;
        }
    close(fd);
    return res;
}

int append_file(char *filename, char *text) {

    int fd, res, len;

    if (!filename || !text)
        return -EINVAL;

    len = strlen(text);
    if (!len) return 0;

    if ( ( fd = open(filename, O_RDWR | O_APPEND) ) < 0) {
        return -errno;
    }

    res = write(fd, text, len);
    close(fd);
    return (res == -1 ? -errno : res);
}

int overwrite_file(char *filename, char *value) {
    FILE *fd;
    int ret = 0;

    if ( !filename || !value )
        return -EINVAL;

    fd = fopen(filename, "w+");
    if (fd == NULL) return -errno;

    if (fprintf(fd, "%s", value) <= 0)
        ret = -errno;

    fclose(fd);
    return ret;
}

int read_file_prop_uid(const char* source, const char *filename, char *uid, char* defaultvalue) {
    FILE *fd;
    char buffer[MAXLINESIZE] = {'\0'};
    char temp_uid[PROPERTY_VALUE_MAX];

    if ((source && filename && uid && defaultvalue) == 0)
        return -1;
    strncpy(uid, defaultvalue, PROPERTY_VALUE_MAX);
    fd = fopen(filename, "r");
    if (fd!=NULL){
        int ret = freadline(fd, buffer);
        if (ret < 0)
           return -1;
        strncpy(uid, buffer, PROPERTY_VALUE_MAX);
        fclose(fd);
    }

    if (property_get(source, temp_uid, "") <= 0) {
        LOGV("Property %s not readable\n", source);
        return -1;
    }
    strncpy(uid, temp_uid, PROPERTY_VALUE_MAX);
    if (strncmp(uid, buffer, PROPERTY_VALUE_MAX) != 0){
        /*need to reopen file in w mode and write the property in the file*/
        fd = fopen(filename, "w");
        if (fd!=NULL){
            fprintf(fd, "%s", uid);
            do_chown(filename, PERM_USER, PERM_GROUP);
            fclose(fd);
        }else{
            LOGV("Can't open file %s \n", filename);
            return -1;
        }
        /*Success + the file was updated*/
        return 1;
    }
    return 0;
}

void flush_aplog(e_aplog_file_t file, const char *mode, char *dir, const char *ts) {
    char log_boot_name[512] = { '\0', };
    int status;

    switch (file) {
    case APLOG:
#ifndef CONFIG_APLOG
        if (!flush_aplog_cmd) {
            int ret = asprintf(&flush_aplog_cmd, "%s %s %s", get_logger_path(),
                               LOGGER_APLOG_PARAM, APLOG_FILE_0);
            if (ret <= 0) {
                flush_aplog_cmd = NULL;
                LOGE("Unable to allocate aplog command\n");
                return;
            }
        }

        if (file_exists(APLOG_FILE_0)) {
            remove(APLOG_FILE_0);
        }
        status = run_command(flush_aplog_cmd, 45);
        if (status != 0)
            LOGE("dump logcat returns status: %d.\n", status);
        do_chown(APLOG_FILE_0, PERM_USER, PERM_GROUP);
#endif
        break;

    case APLOG_BOOT:
        if ((mode == NULL) || (dir == NULL) || (ts == NULL)) {
            LOGE("invalid parameters\n");
            return;
        }

        if (!flush_aplog_boot_cmd) {
            int ret = asprintf(&flush_aplog_boot_cmd, "%s %s %s",
                               get_logger_path(), LOGGER_APLOG_PARAM,
                               APLOG_FILE_BOOT);
            if (ret <= 0) {
                flush_aplog_boot_cmd = NULL;
                LOGE("Unable to allocate aplog boot command\n");
                return;
            }
        }

        snprintf(log_boot_name, sizeof(log_boot_name)-1, "%s/%s_%s_%s",
                dir, strrchr(APLOG_FILE_BOOT,'/')+1, mode, ts);

        status = run_command(flush_aplog_boot_cmd, 45);
        if (status != 0) {
            LOGE("flush ap log from boot returns status: %d.\n", status);
            return;
        }

        if(file_exists(APLOG_FILE_BOOT)) {
            do_copy(APLOG_FILE_BOOT, log_boot_name, MAXFILESIZE);
            remove(APLOG_FILE_BOOT);
        }
        break;

    default:
        LOGE("invalid logfile parameter\n");
        break;
    }
}

static void get_bplog_file(int modem_instance, char *value, int length, char *default_value) {
    char property_name[PATHMAX];
    char property[PROPERTY_VALUE_MAX];
    char *retval = default_value;

    if (modem_instance == 0)
        snprintf(property_name, sizeof(property_name), "persist.vendor.service.mts.output");
    else
        snprintf(property_name, sizeof(property_name), "persist.vendor.sys.mts%d.output", modem_instance + 1);

    if (property_get(property_name, property, NULL))
        retval = property;
    else
        LOGE("Property %s not readable, Using default: %s\n", property_name, default_value);

    snprintf(value, length, "%s", retval);
}

bool can_attach_bplog(int instance) {
    char value[PROPERTY_VALUE_MAX];
    char property_name[PATHMAX];

    if (instance == 0)
        snprintf(property_name, sizeof(property_name), "persist.vendor.service.mts.output_type");
    else
        snprintf(property_name, sizeof(property_name), "persist.vendor.sys.mts%d.output_type", instance+1);

    property_get(property_name, value, "f");
    if (!strcmp("f", value))
        return 1;
    return 0;
}

static void copy_bplogs(const char *extra, char *dir, int limit, int instance, int start_index) {
    char logfile[PATHMAX];

    if (!can_attach_bplog(instance))
        return;

    get_bplog_file(instance, logfile, PATHMAX, BPLOG_FILE_0);
    char *extension = BPLOG_FILE_EXT;

    do_copy_circular(logfile, dir, extension, extra, limit, start_index, 1);
}

static void copy_aplogs(const char *extra, char *dir, int limit, int start_index) {
    unsigned int cnt_len = 1;
    char value[PROPERTY_VALUE_MAX];
#ifndef CONFIG_APLOG
    flush_aplog(APLOG, NULL, NULL, NULL);
#endif
    property_get(PROP_APLOG_ROT_CNT, value, "1");
    /* logcat uses log10 to compute this out of an integer,
     * in our case, since the value is already string and strlen
     * gives the same output, use it*/
    cnt_len = strlen(value);

    do_copy_circular(APLOG_FILE_0, dir, "", extra, limit, start_index, cnt_len);
#ifndef CONFIG_APLOG
    remove(APLOG_FILE_0);
#endif
}

void do_logs_copy(int type, int type_extra_param, const char *dir,
    const char *filename_tag, off_t limit) {
    int start_index = 0;
    int collection_mode = 0;

    filename_tag = (filename_tag == NULL) ? "" : filename_tag;
    if (type == APLOG_TYPE)
        copy_aplogs(filename_tag, (char *)dir, limit, start_index);

    if (type == BPLOG_TYPE_OLD)
        start_index = 1;
    else if (type != BPLOG_TYPE)
        return;

    collection_mode = cfg_collection_mode_modem();
    if (type_extra_param < 0 || collection_mode == COLLECT_BPLOG_CRASHING_ALL) {
        type_extra_param = get_modem_count();
        while (type_extra_param--)
            copy_bplogs(filename_tag, (char *)dir, limit, type_extra_param, start_index);
    }
    else if (collection_mode == COLLECT_BPLOG_CRASHING_MODEM)
        copy_bplogs(filename_tag, (char *)dir, limit, type_extra_param, start_index);
}

void do_log_copy(char *mode, char *dir, const char* timestamp, int type) {
    char extra[PATHMAX];
    snprintf(extra, sizeof(extra), "_%s_%s", mode, timestamp);

    do_logs_copy(type, 0, dir, extra, MAXFILESIZE);
}

int copy_log(const char *src_dir, const char *src_file, e_match match,
             const char *dst_dir, const char *dst_file, bool to_utf8)
{
    char dst[PATHMAX];
    char src[PATHMAX];

    /* Check if the source file is available */
    if (match == MATCH_EXACT) {
        snprintf(src, sizeof(src), "%s/%s", src_dir, src_file);
        if (!file_exists(src))
            return -1;
    }
    else {
        char base[PATHMAX];
        if (1 != find_matching_file(src_dir, src_file, base))
            return -1;

        snprintf(src, sizeof(src), "%s/%s", src_dir, base);
    }


    /* Copy to the destination */
    snprintf(dst, sizeof(dst), "%s/%s", dst_dir, dst_file);
    if (to_utf8)
        do_copy_utf16_to_utf8(src, dst);
    else
        do_copy_eof(src, dst);
    LOGI("%s: Logs %s copied to : %s", __FUNCTION__, src, dst);

    return 0;
}

int copy_log_simple(const char *src, const char *dst_dir)
{
    char src1[PATHMAX], src2[PATHMAX];
    char *src_dir, *src_file;

    snprintf(src1, sizeof(src1), "%s", src);
    snprintf(src2, sizeof(src2), "%s", src);
    src_dir = dirname(src1);
    src_file = basename(src2);

    return copy_log(src_dir, src_file, MATCH_EXACT, dst_dir, src_file, FALSE);
}

/*
* Name          : str_simple_replace
* Description   : replace the searched sequence by the replace sequence
*                   warning : it only works with sequence with the same size
* Parameters    :
*   char *str         -> full string to be processed
*   char *search      -> sequence to be replaced
*   char * replace    ->  string sequence used to replace searched sequence
* @return 0 on success, -1 on error or nothing to replace. */
int str_simple_replace(char *str, char *search, char *replace)
{
    char *f;
    //warning search and replace should have the same size
    if (strlen(search)!= strlen(replace)){
        LOGE("%s: str_simple_replace : size error", __FUNCTION__);
        return -1;
    }
    f = strstr(str, search);
    if (f != NULL) {
        strncpy(f,replace,strlen(replace));
        return 0;
    }else{
        //nothing to replace => error
        return -1;
    }
}

/**
 * @brief extracts the parent directory of the input dir.
 *
 * @param dir        : input directory
 * @param parent_dir : parent path of input directory
 * @return : 0 if succeeds. -1 if fails.
 */
int get_parent_dir( char * dir, char *parent_dir )
{
    char path[PATHMAX];
    char * ptr;

    if ( !dir || !parent_dir )
        return -EINVAL;

    strncpy(path, dir, PATHMAX-1);
    path[PATHMAX-1] = '\0';
    ptr = strrchr(path, '/');

    if ( !ptr || strlen(path) <= 1 )
        return -1;

    /* Manage case path terminates by '/' character */
    if ( *(ptr + 1) == '\0' ) {
        *(ptr) = '\0';
        ptr = strrchr(path, '/');
        if (!ptr)
            return -1;
    }
    *(ptr + 1) = '\0';

    if ( strlen(path) == 0 )
        return -1;

    strncpy( parent_dir, path, PATHMAX-1);
    return 0;
}

long get_sd_size()
{
    FILE *fd;
    long l_size=0;
    //first calculate SDSIZE folder with the result of a du command
    int status = run_command(SDSIZE_SYSTEM_CMD, 15);
    if (status != 0){
        LOGE("get_sd_size status: %d.\n", status);
    }
    //then read the result
    fd = fopen(SDSIZE_CURRENT_LOG, "r");
    if (fd == NULL){
        LOGE("can not open file: %s\n", SDSIZE_CURRENT_LOG);
        //if size could not be calculated, we consider size at zero value
        return 0;
    }
    if (fscanf(fd, "%ld", &l_size)==EOF) {
        l_size = 0;
    }
    if (fd != NULL){
        fclose(fd);
    }
    return l_size;
}

int sdcard_allowed()
{
    /* Does current crashlog mode allow SDcard storage ?*/
    if ( !CRASHLOG_MODE_SD_STORAGE(g_crashlog_mode) ) {
        LOGD("%s : Current crashlog mode is %s - SDCard storage disabled.\n", __FUNCTION__, CRASHLOG_MODE_NAME(g_crashlog_mode));
        return 0;
    }
    //now check remain size on SD
    if (get_sd_size() > current_sd_size_limit){
        LOGE("SD not allowed - current_sd_size_limit reached: %ld.\n", current_sd_size_limit);
        return 0;
    }else{
        return 1;
    }
}

/**
 * Read a one word string from file
 *
 * @param file path
 * @param string to be allocated before
 * @return string length, 0 if none, -errno code if error
 */
int file_read_string(const char *file, char *string) {
    FILE *fd;
    int ret;

    if (!file || !string)
        return -EINVAL;

    if (!file_exists(file))
        return -ENOENT;

    fd = fopen(file, "r");
    if (!fd)
        return -errno;;

    ret = fscanf(fd, "%s", string);
    if (ret == EOF && ferror(fd)) {
        ret = -errno;
        clearerr(fd);
        fclose(fd);
        return ret;
    } else if (ret != 1) {
        fclose(fd);
        return 0;
    }

    fclose(fd);

    return strlen(string);
}

/**
 * Search if a directory contains a file name, could be exact or partial
 *
 * @param dir to search in
 * @param filename to look at
 * @param exact macth of file name or partial
 * @return number of matched files, -errno on errors
 */
int dir_contains(const char *dir, const char *filename, bool exact) {
    int ret, count = 0;
    struct dirent **filelist;
    char *name;

    if (!dir || !filename)
        return -EINVAL;

    ret = scandir(dir, &filelist, 0, 0);
    if (ret < 0)
        return -errno;

    while (ret--) {
        name = filelist[ret]->d_name;
        if (exact) {
            if (!strcmp(name, filename))
                count++;
        } else {
            if (strstr(name, filename))
                count++;
        }
        free(filelist[ret]);
    }

    free(filelist);

    return count;
}

int write_binary_file(const char *path, const unsigned char *buffer, unsigned int buffer_size) {
    FILE * fp;

    if (!path || !buffer || buffer_size <=0)
        return -EINVAL;

    fp = fopen (path, "wb");

    if (fp == NULL) {
        return -errno;
    }

    fwrite (buffer, sizeof(char), buffer_size, fp);
    fclose (fp);
    return 0;
}

int read_binary_file(const char *path, unsigned char *buffer, unsigned int buffer_size) {
    int ret_val;
    FILE * fp;

    if (!path || !buffer || buffer_size <=0)
        return -EINVAL;

    fp = fopen (path, "rb");

    if (fp == NULL) {
        return -errno;
    }

    ret_val = fread (buffer, sizeof(char), buffer_size, fp);
    fclose (fp);
    return ret_val;
}

int read_full_binary_file(const char *path, unsigned long *size, void **data) {
    FILE *f;
    long _size;
    void *buf;
    int err;

    if (!path || !data || !size) {
        errno = EINVAL;
        return -1;
    }

    f = fopen(path, "rb");
    if (!f)
        return -1;

    if (fseek(f, 0, SEEK_END) == -1) {
        err = errno;
        goto close;
    }

    _size = ftell(f);
    if (_size == -1) {
        err = errno;
        goto close;
    } else if (!_size) {
        err = ERANGE;
        goto close;
    }

    if (fseek(f, 0, SEEK_SET) == -1) {
        err = errno;
        goto close;
    }

    buf = malloc(_size);
    if (!buf) {
        err = ENOMEM;
        goto close;
    }

    if (fread(buf, 1, _size, f) != (unsigned)_size) {
        err = EBADF;
        goto free;
    }

    fclose(f);

    *data = buf;
    *size = (unsigned)_size;

    return 0;

  free:
    free(buf);
  close:
    fclose(f);
    errno = err;
    return -1;
}

int read_dev_mem_region(unsigned long addr, unsigned long size, void *data) {
    FILE *f;
    int err;

    if (!size || !data ) {
        errno = EINVAL;
        return -1;
    }

    f = fopen(DEV_MEM_FILE, "rb");
    if (!f)
        return -1;

    if (fseek(f, addr, SEEK_SET) == -1) {
        err = errno;
        goto close;
    }

    if (fread(data, 1, size, f) != size) {
        err = EBADF;
        goto close;
    }

    fclose(f);

    return 0;

  close:
    fclose(f);
    errno = err;
    return -1;
}
