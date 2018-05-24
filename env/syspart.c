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

#include <syspart.h>
#include <utils.h>
#include <envdata.h>

#define MAX_INFO_SIZE 1024

EFI_STATUS open_cfg_file(EFI_FILE_HANDLE root, EFI_FILE_HANDLE *fh,
			 UINT64 mode)
{
	return uefi_call_wrapper(root->Open, 5, root, fh, ENV_FILE_NAME, mode,
				 EFI_FILE_ARCHIVE | EFI_FILE_HIDDEN |
				 EFI_FILE_SYSTEM);
}

EFI_STATUS close_cfg_file(EFI_FILE_HANDLE root, EFI_FILE_HANDLE fh)
{
	return uefi_call_wrapper(root->Close, 1, fh);
}

EFI_STATUS read_cfg_file(EFI_FILE_HANDLE fh, VOID *buffer)
{
	UINTN readlen = sizeof(BG_ENVDATA);
	return uefi_call_wrapper(fh->Read, 3, fh, &readlen, buffer);
}

EFI_STATUS enumerate_cfg_parts(EFI_FILE_HANDLE *roots,
			       EFI_DEVICE_PATH **root_devices,
			       UINTN *numHandles)
{
	EFI_STATUS status;
	UINTN rootCount = 0;

	if (!roots || !numHandles) {
		Print(L"Invalid parameter in system partition enumeration.\n");
		return EFI_INVALID_PARAMETER;
	}
	for (UINTN index = 0; index < volume_count && rootCount < *numHandles;
	     index++) {
		EFI_FILE_HANDLE fh = NULL;
		if (!volumes[index].root) {
			continue;
		}
		status = open_cfg_file(volumes[index].root, &fh,
				       EFI_FILE_MODE_READ);
		if (status == EFI_SUCCESS) {
			Print(L"Config file found on volume %d.\n", index);
			roots[rootCount] = volumes[index].root;
			root_devices[rootCount] = volumes[index].devpath;
			rootCount++;
			status = close_cfg_file(volumes[index].root, fh);
			if (EFI_ERROR(status)) {
				Print(L"Could not close config file on "
				      L"partition %d.\n",
				      index);
			}
		}
	}
	*numHandles = rootCount;
	Print(L"%d config partitions detected.\n", rootCount);
	return EFI_SUCCESS;
}

EFI_STATUS filter_cfg_parts(EFI_FILE_HANDLE *roots,
			    EFI_DEVICE_PATH **root_devices, UINTN *numHandles)
{
	BOOLEAN only_envs_on_bootdevice = FALSE;

	Print(L"Config filter: \n");
	for (UINTN index = 0; index < *numHandles; index++) {
		EFI_FILE_HANDLE fh = NULL;
		EFI_STATUS status;
		BG_ENVDATA env;

		status = open_cfg_file(roots[index], &fh, EFI_FILE_MODE_READ);
		if (EFI_ERROR(status)) {
			return status;
		}

		status = read_cfg_file(fh, &env);
		if (EFI_ERROR(status)) {
			return status;
		}

		if (IsOnBootDevice(root_devices[index]) &&
		    (env.status_flags & ENV_STATUS_FAILSAFE)) {
			only_envs_on_bootdevice = TRUE;
		};

		status = close_cfg_file(roots[index], fh);
		if (EFI_ERROR(status)) {
			return status;
		}
	}

	if (!only_envs_on_bootdevice) {
		// nothing to do
		return EFI_SUCCESS;
	}

	Print(L"Fail-Safe Mode enabled.\n");
	UINTN index = 0;
	do {
		if (!IsOnBootDevice(root_devices[index])) {
			for (UINTN j = index; j < *numHandles-1; j++) {
				roots[j] = roots[j+1];
				root_devices[j] = root_devices[j+1];
			}
			mfree(roots[*numHandles-1]);
			mfree(root_devices[*numHandles-1]);
			(*numHandles)--;
			Print(L"Filtered Config #%d\n", index);
		}
		index++;
	} while(index < *numHandles);

	Print(L"Remaining Config Partitions: %d\n", *numHandles);
	return EFI_SUCCESS;
}
