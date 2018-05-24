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

#ifndef __H_SYSPART__
#define __H_SYSPART__

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <efipciio.h>
#include "bootguard.h"

EFI_STATUS open_cfg_file(EFI_FILE_HANDLE root, EFI_FILE_HANDLE *fh,
			 UINT64 mode);
EFI_STATUS close_cfg_file(EFI_FILE_HANDLE root, EFI_FILE_HANDLE fh);
EFI_STATUS read_cfg_file(EFI_FILE_HANDLE fh, VOID *buffer);
EFI_STATUS enumerate_cfg_parts(UINTN *config_volumes, UINTN *maxHandles);
EFI_STATUS filter_cfg_parts(UINTN *config_volumes, UINTN *maxHandles);

#endif // __H_SYSPART__
