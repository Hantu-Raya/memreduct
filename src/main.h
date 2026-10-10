// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#pragma once

#include "routine.h"

#include "resource.h"
#include "app.h"

DEFINE_GUID (GUID_TrayIcon, 0xAE9053F0, 0x8D59, 0x4803, 0x9A, 0xBB, 0x74, 0xAF, 0xE6, 0x6B, 0x5F, 0xD2);


#define TIMER 1000
#define UID 1337

#define LANG_SUBMENU 1
#define LANG_MENU 4

#define TRAY_SUBMENU_1 4
#define TRAY_SUBMENU_2 5
#define TRAY_SUBMENU_3 6

#define DEFAULT_AUTOREDUCT_VAL 90
#define DEFAULT_AUTOREDUCTINTERVAL_VAL 30

// minimum pause (in seconds) between usage-based automatic cleanups
#define AUTOREDUCT_COOLDOWN 30

// pagefile guard: skip modified page list flush when pagefile usage is at or above this percent
#define DEFAULT_PAGEFILE_GUARD 90

// smart standby purge (ISLC-style): purge standby when standby >= X MB and free < Y MB
#define DEFAULT_SMART_STANDBY_MB 1024
#define DEFAULT_SMART_FREE_MB 1024
#define SMART_COOLDOWN 30

// posted by the cleaning worker thread when it has finished
#define RM_CLEANDONE (WM_APP + 0x100)

#define APP_MAX_PAGEFILES 16

// main listview rows
#define LV_ROW_COMMIT 9
#define LV_ROW_LISTS 12
#define LV_ROW_PAGEFILES 15

#define LV_GROUP_COMMIT 3
#define LV_GROUP_LISTS 4
#define LV_GROUP_PAGEFILES 5

#define DEFAULT_DANGER_LEVEL 90
#define DEFAULT_WARNING_LEVEL 70

// colors
#define TRAY_COLOR_BLACK RGB(0x00, 0x00, 0x00)
#define TRAY_COLOR_WHITE RGB(0xFF, 0xFF, 0xFF)
#define TRAY_COLOR_TEXT RGB(0xFF, 0xFF, 0xFF)
#define TRAY_COLOR_BG RGB(0x00, 0x80, 0x40)
#define TRAY_COLOR_WARNING RGB(0xFF, 0x80, 0x40)
#define TRAY_COLOR_DANGER RGB(0xEC, 0x1C, 0x24)

// memory cleaning mask
#define REDUCT_WORKINGSET 0x01
#define REDUCT_SYSTEMFILECACHE 0x02
#define REDUCT_STANDBYPRIORITY0LIST 0x04
#define REDUCT_STANDBYLIST 0x08
#define REDUCT_MODIFIEDLIST 0x10
#define REDUCT_COMBINEMEMORYLISTS 0x20
#define REDUCT_REGISTRYCACHE 0x40
#define REDUCT_MODIFIEDFILECACHE 0x80

// memory cleaning values
#define REDUCT_MASK_ALL (REDUCT_WORKINGSET | \
REDUCT_SYSTEMFILECACHE | \
REDUCT_STANDBYPRIORITY0LIST | \
REDUCT_STANDBYLIST | \
REDUCT_MODIFIEDLIST | \
REDUCT_COMBINEMEMORYLISTS | \
REDUCT_REGISTRYCACHE | \
REDUCT_MODIFIEDFILECACHE)

#define REDUCT_MASK_DEFAULT (REDUCT_WORKINGSET | REDUCT_SYSTEMFILECACHE | REDUCT_STANDBYPRIORITY0LIST | REDUCT_REGISTRYCACHE | REDUCT_COMBINEMEMORYLISTS | REDUCT_MODIFIEDFILECACHE)
#define REDUCT_MASK_FREEZES (REDUCT_STANDBYLIST | REDUCT_MODIFIEDLIST)

typedef struct _STATIC_DATA
{
	HDC hdc;
	HDC hdc_mask;
	HBITMAP hbitmap;
	HBITMAP hbitmap_mask;
	HFONT hfont;
	RECT icon_size;
	ULONG ms_prev;
	ULONG pagefile_rows;
	LONG64 smart_last;
	volatile LONG is_cleaning;
} STATIC_DATA, *PSTATIC_DATA;

typedef enum _CLEANUP_SOURCE_ENUM
{
	SOURCE_AUTO,
	SOURCE_MANUAL,
	SOURCE_HOTKEY,
	SOURCE_CMDLINE,
	SOURCE_SMART
} CLEANUP_SOURCE_ENUM, *PCLEANUP_SOURCE_ENUM;

typedef struct _APP_PAGEFILE
{
	WCHAR name[128];
	ULONG64 total_bytes;
	ULONG64 used_bytes;
	ULONG64 peak_bytes;
	ULONG64 min_bytes;
	ULONG64 max_bytes;
} APP_PAGEFILE, *PAPP_PAGEFILE;

typedef struct _APP_MEMORY_EX
{
	ULONG64 commit_used;
	ULONG64 commit_limit;
	ULONG64 commit_peak;
	ULONG64 standby_bytes;
	ULONG64 modified_bytes;
	ULONG64 modified_pagefile_bytes;
	ULONG64 free_bytes;
	BOOLEAN is_lists_valid;
	ULONG pagefile_count;
	APP_PAGEFILE pagefiles[APP_MAX_PAGEFILES];
} APP_MEMORY_EX, *PAPP_MEMORY_EX;

// everything the cleaning worker needs, captured on the UI thread
typedef struct _APP_CLEAN_CONTEXT
{
	HWND hwnd;
	PR_STRING exclusions;
	ULONG64 freed_bytes;
	CLEANUP_SOURCE_ENUM src;
	ULONG mask;
	ULONG pagefile_guard;
	ULONG notification_flags;
	WCHAR regions[0x200];
	WCHAR notes[0x100];
} APP_CLEAN_CONTEXT, *PAPP_CLEAN_CONTEXT;


