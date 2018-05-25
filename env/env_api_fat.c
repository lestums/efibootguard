/*
 * EFI Boot Guard
 *
 * Copyright (c) Siemens AG, 2017
 *
 * Authors:
 *  Andreas Reichel <andreas.reichel.ext@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier:	GPL-2.0
 */

#include "env_api.h"
#include "env_disk_utils.h"
#include "env_config_partitions.h"
#include "env_config_file.h"
#include "uservars.h"
#include "test-interface.h"
#include "ebgpart.h"

bool bgenv_verbosity = false;

EBGENVKEY bgenv_str2enum(char *key)
{
	if (strncmp(key, "kernelfile", strlen("kernelfile") + 1) == 0) {
		return EBGENV_KERNELFILE;
	}
	if (strncmp(key, "kernelparams", strlen("kernelparams") + 1) == 0) {
		return EBGENV_KERNELPARAMS;
	}
	if (strncmp(key, "watchdog_timeout_sec",
		    strlen("watchdog_timeout_sec") + 1) == 0) {
		return EBGENV_WATCHDOG_TIMEOUT_SEC;
	}
	if (strncmp(key, "revision", strlen("revision") + 1) == 0) {
		return EBGENV_REVISION;
	}
	if (strncmp(key, "ustate", strlen("ustate") + 1) == 0) {
		return EBGENV_USTATE;
	}
	if (strncmp(key, "in_progress", strlen("in_progress") + 1) == 0) {
		return EBGENV_IN_PROGRESS;
	}
	if (strncmp(key, "env_status_failsafe",
		    strlen("env_status_failsafe") + 1) == 0) {
		return EBGENV_FAILSAFE;
	}
	return EBGENV_UNKNOWN;
}

void bgenv_be_verbose(bool v)
{
	bgenv_verbosity = v;
	ebgpart_beverbose(v);
}

bool read_env(CONFIG_PART *part, BG_ENVDATA *env)
{
	if (!part) {
		return false;
	}
	if (part->not_mounted) {
		/* mount partition before reading config file */
		if (!mount_partition(part)) {
			return false;
		}
	} else {
		VERBOSE(stdout, "Read config file: mounted to %s\n",
			part->mountpoint);
	}
	FILE *config;
	if (!(config = open_config_file(part, "rb"))) {
		return false;
	}
	bool result = true;
	if (!(fread(env, sizeof(BG_ENVDATA), 1, config) == 1)) {
		VERBOSE(stderr, "Error reading environment data from %s\n",
			part->devpath);
		if (feof(config)) {
			VERBOSE(stderr, "End of file encountered.\n");
		}
		result = false;
	}
	if (close_config_file(config)) {
		VERBOSE(stderr,
			"Error closing environment file after reading.\n");
	};
	if (part->not_mounted) {
		unmount_partition(part);
	}
	return result;
}

bool write_env(CONFIG_PART *part, BG_ENVDATA *env)
{
	if (!part) {
		return false;
	}
	if (part->not_mounted) {
		/* mount partition before reading config file */
		if (!mount_partition(part)) {
			return false;
		}
	} else {
		VERBOSE(stdout, "Read config file: mounted to %s\n",
			part->mountpoint);
	}
	FILE *config;
	if (!(config = open_config_file(part, "wb"))) {
		VERBOSE(stderr, "Could not open config file for writing.\n");
		return false;
	}
	bool result = true;
	if (!(fwrite(env, sizeof(BG_ENVDATA), 1, config) == 1)) {
		VERBOSE(stderr, "Error saving environment data to %s\n",
			part->devpath);
		result = false;
	}
	if (close_config_file(config)) {
		VERBOSE(stderr,
			"Error closing environment file after writing.\n");
		result = false;
	};
	if (part->not_mounted) {
		unmount_partition(part);
	}
	return result;
}

CONFIG_PART config_parts[ENV_NUM_CONFIG_PARTS];
BG_ENVDATA envdata[ENV_NUM_CONFIG_PARTS];

bool bgenv_init()
{
	memset((void *)&config_parts, 0,
	       sizeof(CONFIG_PART) * ENV_NUM_CONFIG_PARTS);
	/* enumerate all config partitions */
	if (!probe_config_partitions(config_parts)) {
		VERBOSE(stderr, "Error finding config partitions.\n");
		return false;
	}
	for (int i = 0; i < ENV_NUM_CONFIG_PARTS; i++) {
		read_env(&config_parts[i], &envdata[i]);
		uint32_t sum = crc32(0, (Bytef *)&envdata[i],
		    sizeof(BG_ENVDATA) - sizeof(envdata[i].crc32));
		if (envdata[i].crc32 != sum) {
			VERBOSE(stderr, "Invalid CRC32!\n");
			/* clear invalid environment */
			memset(&envdata[i], 0, sizeof(BG_ENVDATA));
			envdata[i].crc32 = crc32(0, (Bytef *)&envdata[i],
			    sizeof(BG_ENVDATA) - sizeof(envdata[i].crc32));
		}
	}
	return true;
}

BGENV *bgenv_open_by_index(uint32_t index)
{
	BGENV *handle;

	/* get config partition by index and allocate handle */
	if (index >= ENV_NUM_CONFIG_PARTS) {
		return NULL;
	}
	if (!(handle = calloc(1, sizeof(BGENV)))) {
		return NULL;
	}
	handle->desc = (void *)&config_parts[index];
	handle->data = &envdata[index];
	return handle;
}

BGENV *bgenv_open_oldest()
{
	uint32_t minrev = 0xFFFFFFFF;
	uint32_t min_idx = 0;

	for (int i = 0; i < ENV_NUM_CONFIG_PARTS; i++) {
		if (envdata[i].revision < minrev) {
			minrev = envdata[i].revision;
			min_idx = i;
		}
	}
	return bgenv_open_by_index(min_idx);
}

BGENV *bgenv_open_latest()
{
	uint32_t maxrev = 0;
	uint32_t max_idx = 0;

	for (int i = 0; i < ENV_NUM_CONFIG_PARTS; i++) {
		if (envdata[i].revision > maxrev) {
			maxrev = envdata[i].revision;
			max_idx = i;
		}
	}
	return bgenv_open_by_index(max_idx);
}

bool bgenv_write(BGENV *env)
{
	CONFIG_PART *part;

	if (!env) {
		return false;
	}
	part = (CONFIG_PART *)env->desc;
	if (!part) {
		VERBOSE(
		    stderr,
		    "Invalid config partition to store environment.\n");
		return false;
	}
	if (!write_env(part, env->data)) {
		VERBOSE(stderr, "Could not write to %s\n",
			part->devpath);
		return false;
	}
	return true;
}

BG_ENVDATA *bgenv_read(BGENV *env)
{
	if (!env) {
		return NULL;
	}
	return env->data;
}

/* TODO: Refactored API has tests with static struct, that cannot be freed. If
 * gcc inlines this function within this translation unit, tests cannot
 * overload the function by weakening. Thus, define it as noinline until tests
 * are redesigned.
 */
__attribute((noinline))
bool bgenv_close(BGENV *env)
{
	if (env) {
		free(env);
		return true;
	}
	return false;
}

static int bgenv_get_uint(char *buffer, uint64_t *type, void *data,
			  unsigned int src, uint64_t t)
{
	int res;

	res = sprintf(buffer, "%u", src);
	if (!data) {
		return res+1;
	}
	strncpy(data, buffer, res+1);
	if (type) {
		*type = t;
	}
	return 0;
}

static int bgenv_get_string(char *buffer, uint64_t *type, void *data,
			    wchar_t *srcstr)
{
	str16to8(buffer, srcstr);
	if (!data) {
		return strlen(buffer)+1;
	}
	strcpy(data, buffer);
	if (type) {
		*type = USERVAR_TYPE_STRING_ASCII;
	}
	return 0;
}

int bgenv_get(BGENV *env, char *key, uint64_t *type, void *data,
	      uint32_t maxlen)
{
	EBGENVKEY e;
	char buffer[ENV_STRING_LENGTH];

	if (!key || maxlen == 0) {
		return -EINVAL;
	}
	e = bgenv_str2enum(key);
	if (!env) {
		return -EPERM;
	}
	if (e == EBGENV_UNKNOWN) {
		if (!data) {
			uint8_t *u;
			uint32_t size;
			u = bgenv_find_uservar(env->data->userdata, key);
			if (!u) {
				return -ENOENT;
			}
			bgenv_map_uservar(u, NULL, NULL, NULL, NULL, &size);
			return size;
		}
		return bgenv_get_uservar(env->data->userdata, key, type, data,
					 maxlen);
	}
	switch (e) {
	case EBGENV_KERNELFILE:
		return bgenv_get_string(buffer, type, data,
					env->data->kernelfile);
	case EBGENV_KERNELPARAMS:
		return bgenv_get_string(buffer, type, data,
					env->data->kernelparams);
	case EBGENV_WATCHDOG_TIMEOUT_SEC:
		return bgenv_get_uint(buffer, type, data,
				      env->data->watchdog_timeout_sec,
				      USERVAR_TYPE_UINT16);
	case EBGENV_REVISION:
		return bgenv_get_uint(buffer, type, data,
				      env->data->revision,
				      USERVAR_TYPE_UINT16);
	case EBGENV_USTATE:
		return bgenv_get_uint(buffer, type, data,
				      env->data->ustate,
				      USERVAR_TYPE_UINT8);
	case EBGENV_IN_PROGRESS:
		return bgenv_get_uint(buffer, type, data,
				      env->data->status_flags &
					ENV_STATUS_IN_PROGRESS,
				      USERVAR_TYPE_UINT8);
	case EBGENV_FAILSAFE:
		return bgenv_get_uint(buffer, type, data,
				      env->data->status_flags &
				        ENV_STATUS_FAILSAFE,
				      USERVAR_TYPE_UINT8);
	default:
		if (!data) {
			return 0;
		}
		return -EINVAL;
	}
}

static long bgenv_convert_to_long(char *value)
{
	long val;
	char *p;

	errno = 0;
	val = strtol(value, &p, 10);
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
	    (errno != 0 && val == 0)) {
		return -errno;
	}
	if (p == value) {
		return -EINVAL;
	}
	return val;
}

int bgenv_set(BGENV *env, char *key, uint64_t type, void *data,
	      uint32_t datalen)
{
	EBGENVKEY e;
	int val;
	char *p;
	char *value = (char *)data;

	if (!key || !data || datalen == 0) {
		return -EINVAL;
	}

	e = bgenv_str2enum(key);
	if (!env) {
		return -EPERM;
	}
	if (e == EBGENV_UNKNOWN) {
		return bgenv_set_uservar(env->data->userdata, key, type, data,
					 datalen);
	}
	switch (e) {
	case EBGENV_REVISION:
		val = bgenv_convert_to_long(value);
		if (val < 0) {
			return -EINVAL;
		}
		env->data->revision = val;
		break;
	case EBGENV_KERNELFILE:
		str8to16(env->data->kernelfile, value);
		break;
	case EBGENV_KERNELPARAMS:
		str8to16(env->data->kernelparams, value);
		break;
	case EBGENV_WATCHDOG_TIMEOUT_SEC:
		val = bgenv_convert_to_long(value);
		if (val < 0) {
			return -EINVAL;
		}
		env->data->watchdog_timeout_sec = val;
		break;
	case EBGENV_USTATE:
		val = bgenv_convert_to_long(value);
		if (val < 0) {
			return -EINVAL;
		}
		env->data->ustate = val;
		break;
	case EBGENV_IN_PROGRESS:
		val = bgenv_convert_to_long(value);
		if (val < 0) {
			return -EINVAL;
		}
		switch(val) {
		case 1:
			env->data->status_flags |= ENV_STATUS_IN_PROGRESS;
			break;
		case 0:
			env->data->status_flags &= ~ENV_STATUS_IN_PROGRESS;
			break;
		default:
			return -EINVAL;
		}
		break;
	case EBGENV_FAILSAFE:
		val = bgenv_convert_to_long(value);
		if (val < 0) {
			return val;
		}
		switch(val) {
		case 1:
			env->data->status_flags |= ENV_STATUS_FAILSAFE;
			break;
		case 0:
			env->data->status_flags &= ~ENV_STATUS_FAILSAFE;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

BGENV *bgenv_create_new(void)
{
	BGENV *env_latest;
	BGENV *env_new;

	env_latest = bgenv_open_latest();
	if (!env_latest) {
		goto create_new_io_error;
	}

	int new_rev = env_latest->data->revision + 1;

	if (!bgenv_close(env_latest)) {
		goto create_new_io_error;
	}

	env_new = bgenv_open_oldest();
	if (!env_new) {
		goto create_new_io_error;
	}

	/* zero fields */
	memset(env_new->data, 0, sizeof(BG_ENVDATA));
	/* update revision field and testing mode */
	env_new->data->revision = new_rev;
	env_new->data->status_flags = ENV_STATUS_IN_PROGRESS;
	/* set default watchdog timeout */
	env_new->data->watchdog_timeout_sec = 30;

	return env_new;

create_new_io_error:
	errno = EIO;
	return NULL;
}
