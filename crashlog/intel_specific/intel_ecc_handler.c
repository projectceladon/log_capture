/* Copyright (C) Intel 2018
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
 * @file intel_ecc_handler.c
 * @brief File containing functions to handle the crashlogd files/directories
 * watcher.
 *
 */

#include "intel_ecc_handler.h"
#include <sys/inotify.h>
#include <sys/types.h>
#include "fsutils.h"
#include "privconfig.h"
#include "crashutils.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <utils.h>

#define LOG_PREFIX "ECC: "
#define ECC_PATH "/sys/devices/system/edac/mc"
#define WATCH_MC "mc"
#define WATCH_DIMM  "dimm"
#define WATCH_KEY "count"
#define UE_COUNT_FILE "dimm_ue_count"
#define CE_COUNT_FILE "dimm_ce_count"
#define COUNT_BACKUP_FILE LOGS_DIR "/ecc_count_backup"

#define MAX_PATH		256
#define MAX_FILE_LEN	128
enum ecc_type{
	ECC_NONE,
	ECC_UE,
	ECC_CE,
};

typedef struct ecc_watch_info {
	int type;
	int count;
	char eventname[MAX_FILE_LEN];
	char path[MAX_PATH];
}ECC_WATCH_INFO;

typedef struct ecc_watch_list{
	struct ecc_watch_info data;
	struct ecc_watch_list * next;
	struct ecc_watch_list * prev;
} ECC_WATCH_LIST;

static ECC_WATCH_LIST ecc_list = {{ECC_NONE,-1,"",""}, 0,0};
/****************************************************************************************/
/**************                    functions	            *****************************/
/****************************************************************************************/

int check_ecc_error(char * filename);
int set_backup_count(char * stream, int size);
int ecc_event_handle(int type, struct ecc_watch_info *entry);

/**
* @brief free the ecc list
*/
void free_ecc_list() {
	LOGD(LOG_PREFIX "%s\n", __FUNCTION__);
	ECC_WATCH_LIST *entry_list = &ecc_list;
	while(entry_list->next && entry_list->next != &ecc_list) {
		ECC_WATCH_LIST * entry = entry_list->next;
		(entry)->prev->next = (entry)->next;
		(entry)->next->prev = (entry)->prev;
		free(entry);
	}
}

/**
* @brief get the event form ecc list
*
* @return event.
*/
struct ecc_watch_info *get_event_entry(ECC_WATCH_LIST *event) {
	ECC_WATCH_LIST *entry_list = &ecc_list;
	if(!event)
		return NULL;

    while(entry_list->next && entry_list->next != &ecc_list) {
		if(!strcmp(entry_list->next->data.path, event->data.path)
			&& !strcmp(entry_list->next->data.eventname, event->data.eventname)){
			LOGD(LOG_PREFIX "%s find path: %s.\n", __FUNCTION__, entry_list->next->data.path);
			return &(entry_list->next->data);
		}
		entry_list = entry_list->next;
	}
    return NULL;
}
/**
* @brief get the dimm path. under mc floder there may have some mcX folder named mc1,mc2...
* under mcX there have some dimmX(X:0-8) floder which collecting ecc error information.
*
* @return 0 on success, -1 on failure.
*/
int get_ecc_path(char * fname){

    DIR	*dir;
	struct dirent	*entry;

	if(!fname)
		return 0;

	dir = opendir(fname);
	if (!dir){
		LOGE(LOG_PREFIX "opendir %s fail %s.\n", fname, strerror(errno));
		return -1;
	}

	for (entry = readdir(dir); entry; entry = readdir(dir)) {
		if (strstr(entry->d_name, WATCH_MC) && (entry->d_type & DT_DIR)) {
			char path[MAX_PATH];
			sprintf(path, "%s/%s", fname, entry->d_name);
			get_ecc_path((char*)&path);

		} else if(strstr(entry->d_name, WATCH_DIMM) && (entry->d_type & DT_DIR)) {
			ECC_WATCH_LIST *entry_list_ce = (ECC_WATCH_LIST*)malloc(sizeof(ECC_WATCH_LIST));
			char path[MAX_PATH];
			sprintf(entry_list_ce->data.path, "%s/%s", fname, entry->d_name);
			strcpy(path, entry_list_ce->data.path);
			strcpy(entry_list_ce->data.eventname, CE_COUNT_FILE);
			if(!get_event_entry(entry_list_ce)) {
				char filename[MAX_PATH];
				sprintf(filename, "%s/%s", path, CE_COUNT_FILE);
				entry_list_ce->data.count = check_ecc_error((char*)&filename);
				entry_list_ce->data.type=ECC_CE;
				ecc_list.next->prev = entry_list_ce;
				entry_list_ce->next = ecc_list.next;
				ecc_list.next = entry_list_ce;
				entry_list_ce->prev = &ecc_list;
			} else {
				free(entry_list_ce);
			}
			ECC_WATCH_LIST *entry_list_ue = (ECC_WATCH_LIST*)malloc(sizeof(ECC_WATCH_LIST));
			strcpy(entry_list_ue->data.path, path);
			strcpy(entry_list_ue->data.eventname, UE_COUNT_FILE);
			if(!get_event_entry(entry_list_ue)) {
				char filename[MAX_PATH];
				sprintf(filename, "%s/%s", path, UE_COUNT_FILE);
				entry_list_ue->data.count = check_ecc_error((char*)&filename);
				entry_list_ue->data.type=ECC_UE;
				ecc_list.next->prev = entry_list_ue;
				entry_list_ue->next = ecc_list.next;
				ecc_list.next = entry_list_ue;
				entry_list_ue->prev = &ecc_list;
			} else {
				free(entry_list_ue);
			}
		}
	}
	closedir(dir);
	return 1;

}
 /**
 * @brief Check the value of the dimm_ce_count/dimm_ue_count.
 * if the value of the count is bigger then zero, it is a valid ecc error
 *
 * @param
 * filename the path of the ecc count
 *
 * @return the value of the count.
 */

int check_ecc_error(char * filename) {
    FILE *fd;
    int res, len, i=0, error_code=0;
    char buff[65] = {0};

	if(!filename)
		return -1;

    fd = fopen(filename, "r");
    if (!fd) {
        LOGE("%s: failed to open file %s - %s\n", __FUNCTION__,
             filename, strerror(errno));
        return -1;
    }

	res = fread(buff, 64, 1, fd);
	fclose(fd);
    if (res < 0) {
        LOGE("%s: failed to read file %s - %s\n", __FUNCTION__,
             filename, strerror(errno));
        return -1;
    }
	len = strlen(buff);

	for(i=0; i < len; i++) {
		if((buff[i] >= '0') && (buff[i] <= '9')) {
			error_code = error_code*10;
			error_code += buff[i]-'0';
		} else {
			if(buff[i] != '\n' && buff[i] != '\r')
				error_code = 0;
			break;
		}
	}
	//LOGD(LOG_PREFIX "error_code= %d \n", error_code);
	return error_code;
}

/**
 * @brief get backup count
 *
 * get the backup count from file
 *
 * param
 * filename: the path of the count
 * return the count
 */

int get_backup_count(char * filename) {
	FILE *fd;
	int ignore=0, count = 0;
	char line[PATHMAX + 1 + 10];
	char *str = line;
	char *ptr;
	if(!filename)
		return 0;

    fd = fopen(COUNT_BACKUP_FILE, "r");
    if (!fd) {
        LOGE("%s: failed to open file %s - %s\n", __FUNCTION__,
             COUNT_BACKUP_FILE, strerror(errno));
        return -1;
    }

	do {
		if (!fgets(line, sizeof(line), fd))
			break;

		str = line;
		do {
			ptr = strstr(str, ":");
			if(ptr) {
				count = 0;
				ignore = 0;
				if(strncmp(str, filename, ptr-str)==0) {
					char c = *(++ptr);
					while((c != '\0') && (c != ';') && (c != '\r' && c != '\n')) {
						if((ignore==0) && (c >= '0') && (c <= '9')) {
							count = count *10;
							count += c - '0';
						} else {
							ignore = 1;
						}
						if(ignore) {
							break;
						}
						c=*(++ptr);
					}
				} else {
					ignore = 1;
				}
				if(ignore == 1) {
					str = strstr(ptr, ";");
					if(!str)
						break;
					str++;
				} else {
					fclose(fd);
					return count;
				}
			} else
				break;
		} while(1);
	} while(1);

	fclose(fd);

	return 0;
}

/**
 * @brief backup count
 *
 * backup count to the file
 * format is filename:count
 *
 */

int set_backup_count(char * stream, int size) {
	FILE *fd;

	if(!stream)
		return 0;

    fd = fopen(COUNT_BACKUP_FILE, "w");
    if (!fd) {
        LOGE("%s: failed to open file %s - %s\n", __FUNCTION__,
             COUNT_BACKUP_FILE, strerror(errno));
        return -1;
    }

	fwrite(stream, size, 1, fd);
	fclose(fd);

	return 0;
}

/**
 * @brief init_ecc_handler
 *
 * init the list of ecc
 *
 */
void init_ecc_handler(){
    if ( !file_exists(COUNT_BACKUP_FILE) ) {
        FILE *fd = fopen(COUNT_BACKUP_FILE, "w");
        if (fd != NULL){
	        fclose(fd);
        }
    }

	ecc_list.next = ecc_list.prev = &ecc_list;
	get_ecc_path(ECC_PATH);
}

/**
 * @brief restore count
 *
 * edac error happens when crashlogd exit.
 * read the count from sysfs crashlogd start again, compare to the backup count.
 * if the count in sysfs larger than the backup count, generate a ecc event
 *
 */
void restore_count(){
    ECC_WATCH_LIST *entry_list = &ecc_list;
    char *stream = NULL;
    while(entry_list->next && entry_list->next != &ecc_list){
		char filename[MAX_PATH];
		int count = 0;
		sprintf(filename, "%s/%s", entry_list->next->data.path, entry_list->next->data.eventname);
		count = get_backup_count(filename);
		LOGD(LOG_PREFIX "restore_count filename %s,backup_count=%d, current_count=%d.\n", filename, count,
			entry_list->next->data.count);
		if((entry_list->next->data.count > 0) || (count > 0)) {
			if(!stream)
				asprintf(&stream, "%s/%s:%d\r\n", entry_list->next->data.path,
					entry_list->next->data.eventname,entry_list->next->data.count);
			else
				asprintf(&stream, "%s%s/%s:%d\r\n", stream, entry_list->next->data.path,
					entry_list->next->data.eventname,entry_list->next->data.count);
			if(count < entry_list->next->data.count){
				ecc_event_handle(entry_list->next->data.type, &(entry_list->next->data));
			}
		}
		entry_list = entry_list->next;
	}
	if(stream) {
		set_backup_count(stream, strlen(stream));
		free(stream);
	}
}

void fatal_ecc_err_check(){
	char command[MAX_PATH] = {"/vendor/bin/rm"};
	init_ecc_handler();
	ECC_WATCH_LIST *entry_list = &ecc_list;

	while(entry_list->next != &ecc_list){
		char filename[MAX_PATH];
		int count = 0;

		sprintf(filename, "%s/%s", entry_list->next->data.path, entry_list->next->data.eventname);
		count = get_backup_count(filename);
		LOGI(LOG_PREFIX "%s: filename: %s,backup_count=%d, current_count=%d.\n", __FUNCTION__,
				filename, count, entry_list->next->data.count);
		if((entry_list->next->data.count > 0) && (count < entry_list->next->data.count)) {
			ecc_event_handle(entry_list->next->data.type, &(entry_list->next->data));
		}
		entry_list = entry_list->next;
	}
	strcat(command, COUNT_BACKUP_FILE);
	run_command((const char *)command, 10);
}

/**
 * @brief dump_dmesg
 *
 * @param
 * filepath: filepath for save dmesg logs
 *
 * @return success return 0, or return error.
 */
int dump_dmesg(const char *filepath) {
    FILE *fd;
    int prev_stdout, prev_stderr;

    if (!filepath)
        return -EINVAL;

    fd = fopen(filepath, "w+");

    if (!fd)
        return -errno;

    prev_stdout = dup(STDOUT_FILENO);
    prev_stderr = dup(STDERR_FILENO);
    dup2(fileno(fd), STDOUT_FILENO);
    dup2(fileno(fd), STDERR_FILENO);

    fflush(stdout);
    run_command("/system/bin/dmesg ", 15);
    fflush(stdout);

    fclose(fd);
    dup2(prev_stdout, STDOUT_FILENO);
    dup2(prev_stderr, STDERR_FILENO);
    close(prev_stdout);
    close(prev_stderr);
    return 0;
}

/**
 * @brief Handle UE/CE events, generate the ECC event
 *
 * @param
 * type: ECC_CE/ECC_UE
 *
 * @return 0 on success, -1 on error.
 */

int ecc_event_handle(int type, struct ecc_watch_info *entry){
    int ret = 0;
    char *key = NULL;
    char *dir = NULL;
    char path[PATHMAX];
    char destion[PATHMAX];
    char count_file[PATHMAX];
    char ecc_type[PATHMAX];
    const char *dateshort = get_current_time_short(1);

	if(!entry)
		return -1;

	if(type == ECC_CE) {
		key = generate_event_id("ECC", "CE");
		strcpy(count_file, "dimm_ce_count");
		strcpy(ecc_type, "ECC_CE");
	}
	else if(type == ECC_UE) {
		key = generate_event_id("ECC", "UE");
		strcpy(count_file, "dimm_ue_count");
		strcpy(ecc_type, "ECC_UE");
	}
	else
		return ret;

	dir = generate_crashlog_dir(MODE_CRASH, key);
    snprintf(path, sizeof(path),"%s/%s", entry->path, "dimm_location");
	if(file_exists(path)) {
		snprintf(destion,sizeof(destion),"%s/%s", dir, "dimm_location");
		do_copy_tail(path, destion, MAXFILESIZE);
	}

	snprintf(path, sizeof(path),"%s/%s", entry->path, "dimm_label");
	if(file_exists(path)) {
		snprintf(destion,sizeof(destion),"%s/%s", dir, "dimm_label");
		do_copy_tail(path, destion, MAXFILESIZE);
	}

	snprintf(path, sizeof(path),"%s/%s", entry->path, count_file);
	if(file_exists(path)) {
		snprintf(destion,sizeof(destion),"%s/%s", dir, count_file);
		do_copy_tail(path, destion, MAXFILESIZE);
	}
    /* dump dmesg logs to ecc event, ecc address could be found in dmesg,
       search key words, EDAC to find error address */
    snprintf(path, sizeof(path),"%s/dmesg_%s", dir, dateshort);
    dump_dmesg((char *)path);

    raise_event(key, "ECC", ecc_type, NULL, dir);
    LOGE("%-8s%-22s%-20s%s %s\n", "ECC", key, get_current_time_long(0),
            ecc_type, dir);
    free(dir);
    free(key);

	return ret;
}

/**
 * @brief handle ecc count
 *
 * read dimm_ce_count and dimm_ue_count, if the value of the count not equal to the last.
 * generate the CE/UE event
 *
 */
void ecc_count_handle(){
    int error_count=0, type=ECC_NONE;
	char *stream = NULL;
	int update=0;

    ECC_WATCH_LIST *entry_list = &ecc_list;
    while(entry_list->next && entry_list->next != &ecc_list) {
		char filename[MAX_PATH];
		sprintf(filename, "%s/%s", entry_list->next->data.path, entry_list->next->data.eventname);
		error_count = check_ecc_error((char*)&filename);
		if (error_count > 0) {
			if(!stream)
				asprintf(&stream, "%s/%s:%d\r\n", entry_list->next->data.path,
					entry_list->next->data.eventname,error_count);
			else
				asprintf(&stream, "%s%s/%s:%d\r\n", stream, entry_list->next->data.path,
					entry_list->next->data.eventname,error_count);

			type = entry_list->next->data.type;
			LOGD(LOG_PREFIX "%s type=%d, orig count=%d, new count=%d\n", entry_list->next->data.path, type, entry_list->next->data.count, error_count);
			if((type > ECC_NONE) && (error_count > entry_list->next->data.count)) {
				entry_list->next->data.count = error_count;
				ecc_event_handle(type, &(entry_list->next->data));
				update=1;
			}
		}
		entry_list = entry_list->next;
    }
	if(update && stream) {
		set_backup_count(stream, strlen(stream));
	}
	if(stream) {
		free(stream);
	}

}

