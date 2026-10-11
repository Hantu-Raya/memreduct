// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#include "routine.h"

#include "main.h"
#include "compat.h"
#include "rapp.h"

#include "resource.h"

STATIC_DATA config = {0};

ULONG limits_arr[13] = {0};
ULONG intervals_arr[13] = {0};

// main listview layout: group titles and labels of the fixed rows (3 rows per group)
static const ULONG app_group_titles[] = {
	IDS_GROUP_1,
	IDS_GROUP_2,
	IDS_GROUP_3,
	IDS_GROUP_COMMIT,
	IDS_GROUP_LISTS,
	IDS_GROUP_PAGEFILES,
};

static const ULONG app_row_labels[LV_ROW_PAGEFILES] = {
	IDS_ITEM_1, IDS_ITEM_2, IDS_ITEM_3, // physical memory
	IDS_ITEM_1, IDS_ITEM_2, IDS_ITEM_3, // pagefile
	IDS_ITEM_1, IDS_ITEM_2, IDS_ITEM_3, // system working set
	IDS_ITEM_1, IDS_ITEM_2, IDS_ITEM_LIMIT, // commit charge
	IDS_ITEM_STANDBY, IDS_ITEM_MODIFIED, IDS_ITEM_FREE, // memory lists
};

INT WINAPIV compare_numbers (
	_In_opt_ PVOID context,
	_In_ LPCVOID ptr1,
	_In_ LPCVOID ptr2
)
{
	ULONG val1, val2;

	val1 = *(const ULONG *)ptr1;
	val2 = *(const ULONG *)ptr2;

	if (val1 < val2)
		return -1;

	if (val1 > val2)
		return 1;

	return 0;
}

VOID _app_generate_array (
	_Out_ _Writable_elements_ (count) PULONG integers,
	_In_ ULONG_PTR count,
	_In_ ULONG value,
	_In_ ULONG max_value
)
{
	PR_HASHTABLE hashtable;
	ULONG_PTR enum_key = 0;
	ULONG_PTR hash_code;
	ULONG index = 0;

	RtlSecureZeroMemory (integers, sizeof (ULONG) * count);

	hashtable = _r_obj_createhashtable (sizeof (BOOLEAN), 0x10, NULL);

	for (index = 1; index < 9; index++)
	{
		_r_obj_addhashtableitem (hashtable, index * 10, NULL);
	}

	for (index = (value > 2) ? (value - 2) : 1; index <= (value + 2); index++)
	{
		if (index >= 1)
			_r_obj_addhashtableitem (hashtable, index, NULL);
	}

	index = 0;

	while (_r_obj_enumhashtable (hashtable, NULL, &hash_code, &enum_key))
	{
		if (hash_code <= max_value)
			integers[index] = (ULONG)hash_code;

		if (++index >= count)
			break;
	}

	qsort_s (integers, count, sizeof (ULONG), &compare_numbers, NULL);

	_r_obj_dereference (hashtable);
}

VOID _app_generate_menu (
	_In_ HMENU hsubmenu,
	_In_ ULONG menu_idx,
	_Out_ _Writable_elements_ (count) PULONG integers,
	_In_ ULONG_PTR count,
	_In_ LPCWSTR format,
	_In_ ULONG value,
	_In_ ULONG max_value,
	_In_ BOOLEAN is_enabled
)
{
	WCHAR buffer[0x40];
	LONG64 menu_value;
	ULONG menu_id, menu_items = 0;
	BOOLEAN is_checked = FALSE;

	_r_menu_setitemtext (hsubmenu, 0, TRUE, _r_locale_getstring (IDS_TRAY_DISABLE));

	_app_generate_array (integers, count, value, max_value);

	for (ULONG i = 0; i < count; i++)
	{
		menu_value = integers[i];

		if (!menu_value)
			continue;

		menu_id = (menu_idx + i);

		_r_str_printf (buffer, RTL_NUMBER_OF (buffer), format, menu_value);

		_r_menu_additem (hsubmenu, menu_id, buffer);

		if (!_r_sys_iselevated ())
			_r_menu_enableitem (hsubmenu, menu_id, FALSE, FALSE);

		if (value == menu_value)
		{
			_r_menu_checkitem (hsubmenu, menu_id, menu_id, MF_BYCOMMAND, menu_id);

			is_checked = TRUE;
		}

		menu_items += 1;
	}

	if (!is_enabled || !is_checked)
		_r_menu_checkitem (hsubmenu, 0, menu_items + 2, MF_BYPOSITION, 0);
}

ULONG _app_getlimitvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"AutoreductValue", DEFAULT_AUTOREDUCT_VAL, NULL), 0, 100);
}

ULONG _app_getintervalvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"AutoreductIntervalValue", DEFAULT_AUTOREDUCTINTERVAL_VAL, NULL), 1, 1440);
}

ULONG _app_getdangervalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"TrayLevelDanger", DEFAULT_DANGER_LEVEL, NULL), 0, 100);
}

ULONG _app_getwarningvalue ()
{
	return _r_calc_clamp (_r_config_getulong_ex (L"TrayLevelWarning", DEFAULT_WARNING_LEVEL, NULL), 0, 100);
}

BOOLEAN _app_config_invertboolean (
	_In_ LPCWSTR key_name,
	_In_ BOOLEAN def_value,
	_In_opt_ LPCWSTR section_name
)
{
	BOOLEAN new_value;

	new_value = !_r_config_getboolean_ex (key_name, def_value, section_name);

	_r_config_setboolean_ex (key_name, new_value, section_name);

	return new_value;
}

ULONG64 _app_getmemoryinfo (
	_Out_ PR_MEMORY_INFO mem_info
)
{
	_r_sys_getmemoryinfo (mem_info);

	return mem_info->physical_memory.used_bytes;
}

ULONG _app_getpagesize ()
{
	static ULONG page_size = 0;
	SYSTEM_BASIC_INFORMATION sbi = {0};

	if (!page_size)
	{
		if (NT_SUCCESS (NtQuerySystemInformation (SystemBasicInformation, &sbi, sizeof (sbi), NULL)) && sbi.PageSize)
		{
			page_size = sbi.PageSize;
		}
		else
		{
			return PAGE_SIZE;
		}
	}

	return page_size;
}

// Commit charge, memory lists and every pagefile individually.
VOID _app_getmemoryinfo_ex (
	_Out_ PAPP_MEMORY_EX out_buffer
)
{
	SYSTEM_PERFORMANCE_INFORMATION perf_info = {0};
	APP_SYSTEM_MEMORY_LIST_INFORMATION mli = {0};
	PSYSTEM_PAGEFILE_INFORMATION_EX pagefile;
	PVOID buffer;
	PAPP_PAGEFILE entry;
	ULONG64 standby_pages = 0;
	ULONG buffer_length = 0x2000;
	ULONG return_length = 0;
	ULONG record_size;
	ULONG page_size;
	ULONG offset = 0;
	ULONG name_length;
	ULONG_PTR name_offset;
	PWCHAR name;
	BOOLEAN is_ex = TRUE;
	NTSTATUS status;

	RtlZeroMemory (out_buffer, sizeof (APP_MEMORY_EX));

	page_size = _app_getpagesize ();

	if (NT_SUCCESS (NtQuerySystemInformation (SystemPerformanceInformation, &perf_info, sizeof (perf_info), NULL)))
	{
		out_buffer->commit_used = (ULONG64)perf_info.CommittedPages * page_size;
		out_buffer->commit_limit = (ULONG64)perf_info.CommitLimit * page_size;
		out_buffer->commit_peak = (ULONG64)perf_info.PeakCommitment * page_size;
	}

	// requires SeProfileSingleProcessPrivilege (elevated)
	if (NT_SUCCESS (NtQuerySystemInformation (SystemMemoryListInformation, &mli, sizeof (mli), NULL)))
	{
		for (ULONG i = 0; i < RTL_NUMBER_OF (mli.PageCountByPriority); i++)
			standby_pages += mli.PageCountByPriority[i];

		out_buffer->standby_bytes = standby_pages * page_size;
		out_buffer->modified_bytes = (ULONG64)mli.ModifiedPageCount * page_size;
		out_buffer->modified_pagefile_bytes = (ULONG64)mli.ModifiedPageCountPageFile * page_size;
		out_buffer->free_bytes = ((ULONG64)mli.FreePageCount + (ULONG64)mli.ZeroPageCount) * page_size;

		out_buffer->is_lists_valid = TRUE;
	}

	// SystemPageFileInformationEx adds configured min/max sizes (win8+)
	buffer = _r_mem_allocate (buffer_length);

	for (;;)
	{
		return_length = 0;
		status = NtQuerySystemInformation (
			is_ex ? SystemPageFileInformationEx : SystemPageFileInformation,
			buffer,
			buffer_length,
			&return_length
		);

		if (NT_SUCCESS (status))
			break;

		if (status == STATUS_INFO_LENGTH_MISMATCH || status == STATUS_BUFFER_TOO_SMALL)
		{
			if (buffer_length == 0x100000)
				break;

			buffer_length = min (max (return_length, buffer_length * 2), 0x100000UL);
			buffer = _r_mem_reallocate (buffer, buffer_length);
		}
		else if (is_ex)
		{
			is_ex = FALSE;
		}
		else
		{
			break;
		}
	}

	if (!NT_SUCCESS (status))
	{
		_r_mem_free (buffer);
		return;
	}

	record_size = is_ex ? (ULONG)sizeof (SYSTEM_PAGEFILE_INFORMATION_EX) : (ULONG)sizeof (SYSTEM_PAGEFILE_INFORMATION);

	while (out_buffer->pagefile_count < APP_MAX_PAGEFILES && offset <= buffer_length && buffer_length - offset >= record_size)
	{
		pagefile = PTR_ADD_OFFSET (buffer, offset);

		// no pagefile configured: a single zeroed record
		if (!pagefile->TotalSize)
			break;

		entry = &out_buffer->pagefiles[out_buffer->pagefile_count++];

		entry->total_bytes = (ULONG64)pagefile->TotalSize * page_size;
		entry->used_bytes = (ULONG64)pagefile->TotalInUse * page_size;
		entry->peak_bytes = (ULONG64)pagefile->PeakUsage * page_size;

		if (is_ex)
		{
			entry->min_bytes = (ULONG64)pagefile->MinimumSize * page_size;
			entry->max_bytes = (ULONG64)pagefile->MaximumSize * page_size;
		}

		name = pagefile->PageFileName.Buffer;
		name_length = pagefile->PageFileName.Length / sizeof (WCHAR);
		name_offset = (ULONG_PTR)name - (ULONG_PTR)buffer;

		// the name must point inside our buffer
		if (name && (ULONG_PTR)name >= (ULONG_PTR)buffer && name_offset <= buffer_length && pagefile->PageFileName.Length <= buffer_length - name_offset)
		{
			// "\??\C:\pagefile.sys" -> "C:\pagefile.sys"
			if (name_length >= 4 && name[0] == L'\\' && name[1] == L'?' && name[2] == L'?' && name[3] == L'\\')
			{
				name += 4;
				name_length -= 4;
			}

			name_length = min (name_length, RTL_NUMBER_OF (entry->name) - 1);

			RtlCopyMemory (entry->name, name, name_length * sizeof (WCHAR));

			entry->name[name_length] = UNICODE_NULL;
		}
		else
		{
			_r_str_printf (entry->name, RTL_NUMBER_OF (entry->name), L"#%" TEXT (PR_ULONG), out_buffer->pagefile_count);
		}

		if (!pagefile->NextEntryOffset || pagefile->NextEntryOffset < record_size || pagefile->NextEntryOffset > buffer_length - offset)
			break;

		offset += pagefile->NextEntryOffset;
	}

	_r_mem_free (buffer);
}

// Caller frees the returned buffer with _r_mem_free.
_Ret_maybenull_
PSYSTEM_PROCESS_INFORMATION _app_queryprocesses ()
{
	PVOID buffer;
	ULONG buffer_length = 0x40000;
	ULONG return_length = 0;
	NTSTATUS status;

	buffer = _r_mem_allocate (buffer_length);

	for (ULONG attempts = 0; attempts < 8; attempts++)
	{
		status = NtQuerySystemInformation (SystemProcessInformation, buffer, buffer_length, &return_length);

		if (status != STATUS_INFO_LENGTH_MISMATCH && status != STATUS_BUFFER_TOO_SMALL)
			break;

		buffer_length = max (buffer_length * 2, return_length + 0x10000);
		buffer = _r_mem_reallocate (buffer, buffer_length);
	}

	if (!NT_SUCCESS (status))
	{
		_r_mem_free (buffer);

		return NULL;
	}

	return buffer;
}

// "game.exe; obs64.exe, foo.exe" (case-insensitive image names)
BOOLEAN _app_isprocessexcluded (
	_In_ PR_STRING exclusions,
	_In_ PUNICODE_STRING image_name
)
{
	PWCHAR ptr = exclusions->buffer;
	PWCHAR token;
	ULONG_PTR length;
	ULONG_PTR name_length;

	if (!image_name->Buffer)
		return FALSE;

	name_length = image_name->Length / sizeof (WCHAR);

	while (*ptr)
	{
		while (*ptr == L';' || *ptr == L',' || *ptr == L' ' || *ptr == L'\t')
			ptr++;

		token = ptr;

		while (*ptr && *ptr != L';' && *ptr != L',')
			ptr++;

		length = ptr - token;

		while (length && (token[length - 1] == L' ' || token[length - 1] == L'\t'))
			length--;

		if (length && length == name_length && _wcsnicmp (token, image_name->Buffer, length) == 0)
			return TRUE;
	}

	return FALSE;
}

// Per-process working set trim that honors the "WorkingSetExclusions" list.
VOID _app_trimworkingsets (
	_In_ PR_STRING exclusions
)
{
	PSYSTEM_PROCESS_INFORMATION processes;
	PSYSTEM_PROCESS_INFORMATION process;
	HANDLE hprocess;
	ULONG pid;

	processes = _app_queryprocesses ();

	if (!processes)
		return;

	process = processes;

	while (TRUE)
	{
		pid = HandleToUlong (process->UniqueProcessId);

		if (pid > 4 && pid != HandleToUlong (NtCurrentProcessId ()) && !_app_isprocessexcluded (exclusions, &process->ImageName))
		{
			hprocess = OpenProcess (PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

			if (hprocess)
			{
				SetProcessWorkingSetSizeEx (hprocess, (SIZE_T)-1, (SIZE_T)-1, 0);

				CloseHandle (hprocess);
			}
		}

		if (!process->NextEntryOffset)
			break;

		process = PTR_ADD_OFFSET (process, process->NextEntryOffset);
	}

	_r_mem_free (processes);
}

#define APP_MM_KEY L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management"

BOOLEAN _app_isclearpagefileenabled ()
{
	ULONG value = 0;
	ULONG size = sizeof (value);

	if (RegGetValueW (HKEY_LOCAL_MACHINE, APP_MM_KEY, L"ClearPageFileAtShutdown", RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS)
		return FALSE;

	return (value != 0);
}

LSTATUS _app_setclearpagefile (
	_In_ BOOLEAN is_enable
)
{
	ULONG value = is_enable ? 1 : 0;

	return RegSetKeyValueW (HKEY_LOCAL_MACHINE, APP_MM_KEY, L"ClearPageFileAtShutdown", REG_DWORD, &value, sizeof (value));
}

FORCEINLINE LPCWSTR _app_getcleanupreason (
	_In_ CLEANUP_SOURCE_ENUM src
)
{
	switch (src)
	{
		case SOURCE_AUTO:
		{
			return L"Cleanup (Auto)";
		}

		case SOURCE_MANUAL:
		{
			return L"Cleanup (Manual)";
		}

		case SOURCE_HOTKEY:
		{
			return L"Cleanup (Hotkey)";
		}

		case SOURCE_CMDLINE:
		{
			return L"Cleanup (Command-line)";
		}

		case SOURCE_SMART:
		{
			return L"Cleanup (Smart standby)";
		}

		default:
		{
			return L"Unknown";
		}
	}
}

_Success_ (NT_SUCCESS (return))
NTSTATUS _app_getvolumemountpoints (
	_Out_ PMOUNTMGR_MOUNT_POINTS_PTR out_buffer,
	_In_ HANDLE hdevice
)
{
	MOUNTMGR_MOUNT_POINT input = {0};
	PMOUNTMGR_MOUNT_POINTS buffer;
	ULONG buffer_length = 0x1000;
	NTSTATUS status;

	*out_buffer = NULL;

	buffer = _r_mem_allocate (buffer_length);

	for (ULONG attempts = 0; attempts < 6; attempts++)
	{
		status = _r_fs_deviceiocontrol (hdevice, IOCTL_MOUNTMGR_QUERY_POINTS, &input, sizeof (input), buffer, buffer_length, NULL);

		if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
			break;

		// the driver reports the required size in the first field
		buffer_length = (buffer->Size > buffer_length) ? buffer->Size : buffer_length * 2;
		buffer = _r_mem_reallocate (buffer, buffer_length);
	}

	if (!NT_SUCCESS (status))
	{
		_r_mem_free (buffer);

		return status;
	}

	*out_buffer = buffer;

	return status;
}

// Opens the volume without mounting a filesystem and checks the device characteristics.
BOOLEAN _app_isfixedvolume (
	_In_ PUNICODE_STRING volume_name
)
{
	FILE_FS_DEVICE_INFORMATION fdi = {0};
	OBJECT_ATTRIBUTES oa = {0};
	IO_STATUS_BLOCK isb;
	HANDLE hvolume;
	NTSTATUS status;

	InitializeObjectAttributes (&oa, volume_name, OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = NtCreateFile (
		&hvolume,
		FILE_READ_ATTRIBUTES | SYNCHRONIZE,
		&oa,
		&isb,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);

	if (!NT_SUCCESS (status))
		return FALSE;

	status = NtQueryVolumeInformationFile (hvolume, &isb, &fdi, sizeof (fdi), FileFsDeviceInformation);

	NtClose (hvolume);

	if (!NT_SUCCESS (status))
		return FALSE;

	if (fdi.DeviceType != FILE_DEVICE_DISK && fdi.DeviceType != FILE_DEVICE_VIRTUAL_DISK)
		return FALSE;

	if (fdi.Characteristics & (FILE_REMOVABLE_MEDIA | FILE_READ_ONLY_DEVICE | FILE_FLOPPY_DISKETTE | FILE_REMOTE_DEVICE))
		return FALSE;

	return TRUE;
}

NTSTATUS _app_flushvolumecache ()
{
	PMOUNTMGR_MOUNT_POINTS object_mountpoints;
	PMOUNTMGR_MOUNT_POINT mountpoint;
	OBJECT_ATTRIBUTES oa = {0};
	IO_STATUS_BLOCK isb;
	UNICODE_STRING us;
	HANDLE hdevice, hvolume;
	NTSTATUS status;

	RtlInitUnicodeString (&us, MOUNTMGR_DEVICE_NAME);

	InitializeObjectAttributes (&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = NtCreateFile (
		&hdevice,
		FILE_READ_ATTRIBUTES | SYNCHRONIZE,
		&oa,
		&isb,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);

	if (!NT_SUCCESS (status))
		return status;

	status = _app_getvolumemountpoints (&object_mountpoints, hdevice);

	if (!NT_SUCCESS (status))
		goto CleanupExit;

	for (ULONG i = 0; i < object_mountpoints->NumberOfMountPoints; i++)
	{
		mountpoint = &object_mountpoints->MountPoints[i];

		us.Length = mountpoint->SymbolicLinkNameLength;
		us.MaximumLength = mountpoint->SymbolicLinkNameLength + sizeof (UNICODE_NULL);
		us.Buffer = PTR_ADD_OFFSET (object_mountpoints, mountpoint->SymbolicLinkNameOffset);

		// fixed local disks only: flushing removable, network or offline volumes can stall for seconds
		if (MOUNTMGR_IS_VOLUME_NAME (&us) && _app_isfixedvolume (&us)) // \\??\\Volume{1111-2222}
		{
			InitializeObjectAttributes (&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);

			status = NtCreateFile (
				&hvolume,
				FILE_WRITE_DATA | SYNCHRONIZE,
				&oa,
				&isb,
				NULL,
				FILE_ATTRIBUTE_NORMAL,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				FILE_OPEN,
				FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
				NULL,
				0
			);

			if (NT_SUCCESS (status))
			{
				status = _r_fs_flushfile (hvolume);

				NtClose (hvolume);
			}
		}
	}

	_r_mem_free (object_mountpoints);

CleanupExit:

	NtClose (hdevice);

	return status;
}

// Runs on the worker thread (or inline for command-line cleaning). No UI and no config access here.
VOID _app_memoryclean_perform (
	_Inout_ PAPP_CLEAN_CONTEXT context
)
{
	MEMORY_COMBINE_INFORMATION_EX combine_info_ex = {0};
	SYSTEM_FILECACHE_INFORMATION sfci = {0};
	SYSTEM_MEMORY_LIST_COMMAND command;
	R_MEMORY_INFO mem_info;
	ULONG64 reduct_after, reduct_before;
	ULONG mask = context->mask;
	NTSTATUS status;

	// difference (before)
	reduct_before = _app_getmemoryinfo (&mem_info);

	// Working set (vista+)
	if ((mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET)
	{
		if (context->exclusions && !_r_obj_isstringempty (context->exclusions))
		{
			_app_trimworkingsets (context->exclusions);
		}
		else
		{
			command = MemoryEmptyWorkingSets;

			status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

			if (!NT_SUCCESS (status))
				_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryEmptyWorkingSets");
		}
	}

	// System file cache
	if ((mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE)
	{
		sfci.MinimumWorkingSet = MAXSIZE_T;
		sfci.MaximumWorkingSet = MAXSIZE_T;

		status = NtSetSystemInformation (SystemFileCacheInformationEx, &sfci, sizeof (SYSTEM_FILECACHE_INFORMATION));

		if (!NT_SUCCESS (status))
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemFileCacheInformation");
	}

	// Flush volume cache (fixed volumes only)
	if ((mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE)
		_app_flushvolumecache ();

	// Modified page list (vista+)
	if ((mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST)
	{
		R_MEMORY_INFO mem_guard;

		// pagefile guard: flushing writes pagefile-backed modified pages into the pagefile,
		// so skip it when the pagefile is already nearly full
		_app_getmemoryinfo (&mem_guard);

		if (context->pagefile_guard && mem_guard.page_file.total_bytes && mem_guard.page_file.percent >= context->pagefile_guard)
		{
			_r_str_printf (
				context->notes,
				RTL_NUMBER_OF (context->notes),
				L"%s: skipped, pagefile %" TEXT (PR_ULONG) L"%% full",
				_r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST),
				mem_guard.page_file.percent
			);
		}
		else
		{
			command = MemoryFlushModifiedList;

			status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

			if (!NT_SUCCESS (status))
				_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryFlushModifiedList");
		}
	}

	// Standby list (vista+)
	if ((mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST)
	{
		command = MemoryPurgeStandbyList;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryPurgeStandbyList");
	}

	// Standby priority-0 list (vista+)
	if ((mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST)
	{
		command = MemoryPurgeLowPriorityStandbyList;

		status = NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (SYSTEM_MEMORY_LIST_COMMAND));

		if (!NT_SUCCESS (status))
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"MemoryPurgeLowPriorityStandbyList");
	}

	// Flush registry cache (win8.1+)
	if (_r_sys_isosversiongreaterorequal (WINDOWS_8_1) && (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
	{
		status = NtSetSystemInformation (SystemRegistryReconciliationInformation, NULL, 0);

		if (!NT_SUCCESS (status))
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemRegistryReconciliationInformation");
	}

	// Combine memory lists (win10+)
	if (_r_sys_isosversiongreaterorequal (WINDOWS_10) && (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
	{
		status = NtSetSystemInformation (SystemCombinePhysicalMemoryInformation, &combine_info_ex, sizeof (MEMORY_COMBINE_INFORMATION_EX));

		if (!NT_SUCCESS (status))
			_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"SystemCombinePhysicalMemoryInformation");
	}

	// difference (after)
	reduct_after = _app_getmemoryinfo (&mem_info);

	context->freed_bytes = (reduct_after < reduct_before) ? (reduct_before - reduct_after) : 0;
}

// Runs on the UI thread: report results and release the context.
VOID _app_memoryclean_finish (
	_In_ PAPP_CLEAN_CONTEXT context
)
{
	WCHAR buffer_cleaned_result[0x100] = {0}, buffer_result[0x400] = {0};
	HWND hwnd = context->hwnd;
	CLEANUP_SOURCE_ENUM src = context->src;

	// time of last cleaning
	if (src != SOURCE_SMART)
		_r_config_setlong64_ex (L"StatisticLastReduct", _r_unixtime_now (), NULL);

	_r_format_bytesize64 (buffer_cleaned_result, RTL_NUMBER_OF (buffer_cleaned_result), context->freed_bytes);

	_r_str_printf (buffer_result, RTL_NUMBER_OF (buffer_result), _r_locale_getstring (IDS_STATUS_CLEANED), buffer_cleaned_result);
	_r_str_appendformat (buffer_result, RTL_NUMBER_OF (buffer_result), L"\r\n\r\n%s:\r\n%s", _r_locale_getstring (IDS_TITLE_3), context->regions);

	if (context->notes[0])
		_r_str_appendformat (buffer_result, RTL_NUMBER_OF (buffer_result), L"\r\n\r\n%s", context->notes);

	if (src == SOURCE_CMDLINE)
	{
		if (_r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL))
		{
			if (!hwnd || !_r_tray_popup (hwnd, &GUID_TrayIcon, context->notification_flags, _r_app_getname (), buffer_result))
				_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, _r_locale_getstring (IDS_SETTINGS_MEMORY), buffer_result);
		}
		else
		{
			_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, _r_locale_getstring (IDS_SETTINGS_MEMORY), buffer_result);
		}
	}
	else if (src != SOURCE_SMART) // smart standby purge stays silent, like ISLC
	{
		if (hwnd && _r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL))
			_r_tray_popup (hwnd, &GUID_TrayIcon, context->notification_flags, _r_app_getname (), buffer_result);
	}

	if (_r_config_getboolean_ex (L"LogCleanResults", FALSE, NULL))
		_r_log_v (LOG_LEVEL_INFO, NULL, _app_getcleanupreason (src), 0, L"%s", buffer_cleaned_result);

	if (hwnd)
		_r_ctrl_enable (hwnd, IDC_CLEAN, TRUE);

	if (context->exclusions)
		_r_obj_dereference (context->exclusions);

	_r_mem_free (context);

	InterlockedExchange (&config.is_cleaning, FALSE);
}

DWORD WINAPI _app_memoryclean_thread (
	_In_ PVOID lparam
)
{
	PAPP_CLEAN_CONTEXT context = lparam;

	_app_memoryclean_perform (context);

	// the UI thread reports results and frees the context
	if (!PostMessageW (context->hwnd, RM_CLEANDONE, 0, (LPARAM)context))
	{
		if (context->exclusions)
			_r_obj_dereference (context->exclusions);

		_r_mem_free (context);

		InterlockedExchange (&config.is_cleaning, FALSE);
	}

	return 0;
}

VOID _app_appendregion (
	_Inout_updates_ (buffer_length) LPWSTR buffer,
	_In_ ULONG_PTR buffer_length,
	_In_ LPCWSTR name,
	_In_opt_ LPCWSTR suffix
)
{
	_r_str_appendformat (buffer, buffer_length, L"- %s%s\r\n", name, suffix ? suffix : L"");
}

VOID _app_memoryclean (
	_In_opt_ HWND hwnd,
	_In_ CLEANUP_SOURCE_ENUM src,
	_In_opt_ ULONG mask
)
{
	WCHAR buffer_region[0x200] = {0};
	PAPP_CLEAN_CONTEXT context;
	HANDLE hthread;
	ULONG flags = NIIF_WARNING;

	if (!_r_config_getboolean_ex (L"IsNotificationsSound", TRUE, NULL))
		flags |= NIIF_NOSOUND;

	if (!_r_sys_iselevated ())
	{
		if (_r_app_runasadmin ())
		{
			if (hwnd)
				DestroyWindow (hwnd);
		}
		else
		{
			if (src == SOURCE_CMDLINE)
			{
				if (hwnd)
					_r_show_message (hwnd, MB_OK | MB_ICONSTOP, _r_locale_getstring (IDS_STATUS_NOPRIVILEGES), NULL);
			}
			else
			{
				if (hwnd)
					_r_tray_popup (hwnd, &GUID_TrayIcon, flags, _r_app_getname (), _r_locale_getstring (IDS_STATUS_NOPRIVILEGES));
			}
		}

		return;
	}

	// a cleaning is already running on the worker thread
	if (config.is_cleaning)
		return;

	if (mask == 0)
		mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

	if (src == SOURCE_AUTO)
	{
		if (!_r_config_getboolean_ex (L"IsAllowStandbyListCleanup", FALSE, NULL))
			mask &= ~REDUCT_MASK_FREEZES; // exclude freezes from autoclean feature ;)
	}

	if ((mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_WORKINGSET), NULL);

	if ((mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE), NULL);

	if ((mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE), NULL);

	if ((mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST), L"*");

	if ((mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_STANDBYLIST), L"*");

	if ((mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0), NULL);

	if ((mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE), L" (win8.1+)");

	if ((mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
		_app_appendregion (buffer_region, RTL_NUMBER_OF (buffer_region), _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS), L" (win10+)");

	StrTrimW (buffer_region, L"\r\n");

	if (src == SOURCE_MANUAL)
	{
		if (!_r_show_confirmmessage (hwnd, _r_locale_getstring (IDS_QUESTION), buffer_region, L"IsShowReductConfirmation", FALSE))
			return;
	}

	if (InterlockedCompareExchange (&config.is_cleaning, TRUE, FALSE))
		return;

	// prevents the usage-based autoclean from re-triggering while a non-smart clean runs
	if (src != SOURCE_SMART)
		_r_config_setlong64_ex (L"StatisticLastReduct", _r_unixtime_now (), NULL);

	context = _r_mem_allocate (sizeof (APP_CLEAN_CONTEXT));

	context->hwnd = hwnd;
	context->src = src;
	context->mask = mask;
	context->notification_flags = flags;
	context->exclusions = _r_config_getstring_ex (L"WorkingSetExclusions", NULL, NULL);

	if (_r_config_getboolean_ex (L"PagefileGuardEnable", TRUE, NULL))
		context->pagefile_guard = _r_calc_clamp (_r_config_getulong_ex (L"PagefileGuardPercent", DEFAULT_PAGEFILE_GUARD, NULL), 1, 100);

	_r_str_copy (context->regions, RTL_NUMBER_OF (context->regions), buffer_region);

	hthread = NULL;

	// cleaning can take tens of seconds on large systems (issue #312): keep the UI responsive
	if (hwnd)
	{
		_r_ctrl_enable (hwnd, IDC_CLEAN, FALSE);

		hthread = CreateThread (NULL, 0, &_app_memoryclean_thread, context, 0, NULL);
	}

	if (hthread)
	{
		NtClose (hthread);
	}
	else
	{
		_app_memoryclean_perform (context);
		_app_memoryclean_finish (context);
	}
}

VOID _app_fontinit (
	_Out_ PLOGFONT logfont,
	_In_ LONG dpi_value
)
{
	RtlZeroMemory (logfont, sizeof (LOGFONT));

	_r_str_copy (logfont->lfFaceName, LF_FACESIZE, L"Lucida Console");

	logfont->lfHeight = _r_dc_fontsizetoheight (8, dpi_value);
	logfont->lfWeight = FW_NORMAL;

	_r_config_getfont_ex (L"TrayFont", logfont, dpi_value, NULL);

	logfont->lfCharSet = DEFAULT_CHARSET;
	logfont->lfQuality = _r_config_getboolean_ex (L"TrayUseAntialiasing", FALSE, NULL) ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;
}

VOID _app_drawbackground (
	_In_ HDC hdc,
	_In_ COLORREF bg_clr,
	_In_ COLORREF pen_clr,
	_In_ COLORREF brush_clr,
	_In_ LPCRECT rect,
	_In_ BOOLEAN is_round
)
{
	HGDIOBJ prev_brush, prev_pen;
	COLORREF prev_clr;

	prev_brush = SelectObject (hdc, GetStockObject (DC_BRUSH));
	prev_pen = SelectObject (hdc, GetStockObject (DC_PEN));

	prev_clr = SetBkColor (hdc, bg_clr);

	SetDCPenColor (hdc, pen_clr);
	SetDCBrushColor (hdc, brush_clr);

	_r_dc_fillrect (hdc, rect, bg_clr);

	if (is_round)
	{
		RoundRect (hdc, rect->left, rect->top, rect->right, rect->bottom, rect->right - 2, rect->right / 2);
	}
	else
	{
		Rectangle (hdc, rect->left, rect->top, rect->right, rect->bottom);
	}

	SelectObject (hdc, prev_brush);
	SelectObject (hdc, prev_pen);

	SetBkColor (hdc, prev_clr);
}

HICON _app_iconcreate (
	_In_opt_ ULONG percent
)
{
	static HICON hicon = NULL;

	R_MEMORY_INFO mem_info;
	WCHAR icon_text[0x08];
	ICONINFO ii = {0};
	R_STRINGREF sr;
	HGDIOBJ prev_bmp, prev_font;
	HICON hicon_new;
	COLORREF bg_color, text_color;
	INT prev_mode;
	BOOLEAN is_border, is_round, is_transparent, has_danger;

	text_color = _r_config_getulong_ex (L"TrayColorText", TRAY_COLOR_TEXT, NULL);
	bg_color = _r_config_getulong_ex (L"TrayColorBg", TRAY_COLOR_BG, NULL);

	is_transparent = _r_config_getboolean_ex (L"TrayUseTransparency", FALSE, NULL);
	is_border = _r_config_getboolean_ex (L"TrayShowBorder", FALSE, NULL);
	is_round = _r_config_getboolean_ex (L"TrayRoundCorners", FALSE, NULL);

	if (percent == 0)
	{
		_app_getmemoryinfo (&mem_info);

		percent = mem_info.physical_memory.percent;
	}

	has_danger = (percent >= _app_getdangervalue ());

	if (has_danger || percent >= _app_getwarningvalue ())
	{
		if (_r_config_getboolean_ex (L"TrayChangeBg", TRUE, NULL))
		{
			bg_color = has_danger ? _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL) : _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);

			is_transparent = FALSE;
		}
		else
		{
			text_color = has_danger ? _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL) : _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);
		}
	}

	// set tray text
	_r_str_fromulong (icon_text, RTL_NUMBER_OF (icon_text), percent);

	_r_obj_initializestringref (&sr, icon_text);

	// draw main device context
	prev_bmp = SelectObject (config.hdc, config.hbitmap);
	prev_font = SelectObject (config.hdc, config.hfont);
	prev_mode = SetBkMode (config.hdc, TRANSPARENT);

	_app_drawbackground (config.hdc, bg_color, is_border ? text_color : bg_color, is_transparent ? text_color : bg_color, &config.icon_size, is_round);

	_r_dc_drawtext (NULL, config.hdc, &sr, &config.icon_size, 0, 0, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX, text_color);

	SetBkMode (config.hdc, prev_mode);

	SelectObject (config.hdc, prev_font);
	SelectObject (config.hdc, prev_bmp);

	// draw mask device context
	prev_bmp = SelectObject (config.hdc_mask, config.hbitmap_mask);
	prev_font = SelectObject (config.hdc_mask, config.hfont);
	prev_mode = SetBkMode (config.hdc_mask, TRANSPARENT);

	_app_drawbackground (config.hdc_mask, TRAY_COLOR_WHITE, is_border ? TRAY_COLOR_BLACK : TRAY_COLOR_WHITE, is_transparent ? TRAY_COLOR_WHITE : TRAY_COLOR_BLACK, &config.icon_size, is_round);

	_r_dc_drawtext (NULL, config.hdc_mask, &sr, &config.icon_size, 0, 0, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX, TRAY_COLOR_BLACK);

	SetBkMode (config.hdc_mask, prev_mode);

	SelectObject (config.hdc_mask, prev_bmp);
	SelectObject (config.hdc_mask, prev_font);

	// create icon
	ii.hbmMask = config.hbitmap_mask;
	ii.hbmColor = config.hbitmap;
	ii.fIcon = TRUE;

	hicon_new = CreateIconIndirect (&ii);

	if (hicon)
		DestroyIcon (hicon);

	hicon = hicon_new;

	return hicon;
}

VOID _app_setrow (
	_In_ HWND hwnd,
	_In_ INT item_id,
	_In_ LPWSTR text,
	_In_ ULONG percent
)
{
	_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, 1, text, I_DEFAULT, I_DEFAULT, I_DEFAULT);
	_r_listview_setitem (hwnd, IDC_LISTVIEW, item_id, 0, NULL, I_DEFAULT, I_DEFAULT, (LPARAM)percent);
}

VOID _app_setrowbytes (
	_In_ HWND hwnd,
	_In_ INT item_id,
	_In_ ULONG64 bytes,
	_In_ ULONG percent
)
{
	WCHAR buffer[0x40];

	_r_format_bytesize64 (buffer, RTL_NUMBER_OF (buffer), bytes);

	_app_setrow (hwnd, item_id, buffer, percent);
}

VOID _app_setrowpercent (
	_In_ HWND hwnd,
	_In_ INT item_id,
	_In_ DOUBLE percent_f
)
{
	WCHAR buffer[0x40];

	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%" TEXT (PR_DOUBLE) L"%%", percent_f);

	_app_setrow (hwnd, item_id, buffer, (ULONG)percent_f);
}

ULONG _app_percentof (
	_In_ ULONG64 value,
	_In_ ULONG64 total
)
{
	if (!total)
		return 0;

	return (ULONG)PR_CALC_PERCENTOF (value, total);
}

// one row per pagefile, rebuilt only when the pagefile set changes
VOID _app_updatepagefilerows (
	_In_ HWND hwnd,
	_In_ PAPP_MEMORY_EX mem_ex
)
{
	WCHAR used_text[0x40], total_text[0x40], buffer[0x100];
	PAPP_PAGEFILE pagefile;

	if (config.pagefile_rows != mem_ex->pagefile_count)
	{
		while (_r_listview_getitemcount (hwnd, IDC_LISTVIEW) > LV_ROW_PAGEFILES)
			_r_listview_deleteitem (hwnd, IDC_LISTVIEW, LV_ROW_PAGEFILES);

		for (ULONG i = 0; i < mem_ex->pagefile_count; i++)
			_r_listview_additem (hwnd, IDC_LISTVIEW, LV_ROW_PAGEFILES + i, mem_ex->pagefiles[i].name, I_DEFAULT, LV_GROUP_PAGEFILES, 0);

		config.pagefile_rows = mem_ex->pagefile_count;
	}

	for (ULONG i = 0; i < mem_ex->pagefile_count; i++)
	{
		pagefile = &mem_ex->pagefiles[i];

		_r_format_bytesize64 (used_text, RTL_NUMBER_OF (used_text), pagefile->used_bytes);
		_r_format_bytesize64 (total_text, RTL_NUMBER_OF (total_text), pagefile->total_bytes);

		_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s / %s", used_text, total_text);

		_r_listview_setitem (hwnd, IDC_LISTVIEW, LV_ROW_PAGEFILES + i, 0, pagefile->name, I_DEFAULT, I_DEFAULT, I_DEFAULT);

		_app_setrow (hwnd, LV_ROW_PAGEFILES + i, buffer, _app_percentof (pagefile->used_bytes, pagefile->total_bytes));
	}
}

// ISLC-style trigger: purge standby only when it is large AND free memory is low
VOID _app_checksmartstandby (
	_In_ HWND hwnd,
	_In_ PAPP_MEMORY_EX mem_ex
)
{
	ULONG64 standby_limit, free_limit;
	LONG64 now;

	if (!mem_ex->is_lists_valid || config.is_cleaning)
		return;

	if (!_r_config_getboolean_ex (L"SmartStandbyEnable", FALSE, NULL))
		return;

	standby_limit = (ULONG64)_r_config_getulong_ex (L"SmartStandbyListMB", DEFAULT_SMART_STANDBY_MB, NULL) * 1048576ULL;
	free_limit = (ULONG64)_r_config_getulong_ex (L"SmartStandbyFreeMB", DEFAULT_SMART_FREE_MB, NULL) * 1048576ULL;

	if (mem_ex->standby_bytes < standby_limit || mem_ex->free_bytes >= free_limit)
		return;

	now = _r_unixtime_now ();

	if (now - config.smart_last < SMART_COOLDOWN)
		return;

	config.smart_last = now;

	_app_memoryclean (hwnd, SOURCE_SMART, REDUCT_STANDBYLIST);
}

VOID CALLBACK _app_timercallback (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ UINT_PTR id_event,
	_In_ ULONG time
)
{
	R_MEMORY_INFO mem_info;
	APP_MEMORY_EX mem_ex;
	HICON hicon = NULL;
	LONG64 timestamp;
	ULONG commit_percent;
	DOUBLE commit_percent_f = 0.0;
	BOOLEAN is_clean = FALSE;

	if (id_event != UID)
		return;

	_app_getmemoryinfo (&mem_info);
	_app_getmemoryinfo_ex (&mem_ex);

	if (mem_ex.commit_limit)
		commit_percent_f = PR_CALC_PERCENTOF (mem_ex.commit_used, mem_ex.commit_limit);

	commit_percent = (ULONG)commit_percent_f;

	// autocleanup functional
	if (_r_sys_iselevated () && !config.is_cleaning)
	{
		if (_r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL))
		{
			if (mem_info.physical_memory.percent >= _app_getlimitvalue ())
			{
				// cooldown prevents cleaning loop when memory usage stays above the limit (issue #191)
				timestamp = _r_unixtime_now () - _r_config_getlong64_ex (L"StatisticLastReduct", 0, NULL);

				if (timestamp >= AUTOREDUCT_COOLDOWN)
					is_clean = TRUE;
			}
		}

		if (!is_clean && _r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL))
		{
			timestamp = _r_unixtime_now () - _r_config_getlong64_ex (L"StatisticLastReduct", 0, NULL);

			if (timestamp >= (_app_getintervalvalue () * 60))
				is_clean = TRUE;
		}

		if (is_clean)
		{
			_app_memoryclean (hwnd, SOURCE_AUTO, 0);
		}
		else
		{
			_app_checksmartstandby (hwnd, &mem_ex);
		}
	}

	// check previous percent to prevent icon redraw
	if (!config.ms_prev || config.ms_prev != mem_info.physical_memory.percent)
	{
		config.ms_prev = mem_info.physical_memory.percent; // store last percentage value (required!)

		hicon = _app_iconcreate (config.ms_prev);
	}

	_r_tray_setinfoformat (
		hwnd,
		&GUID_TrayIcon,
		hicon,
		L"%s: %" TEXT (PR_DOUBLE) L"%%\r\n%s: %" TEXT (PR_DOUBLE) L"%%\r\n%s: %" TEXT (PR_DOUBLE) L"%%\r\n%s: %" TEXT (PR_DOUBLE) L"%%",
		_r_locale_getstring (IDS_GROUP_1),
		mem_info.physical_memory.percent_f,
		_r_locale_getstring (IDS_GROUP_2),
		mem_info.page_file.percent_f,
		_r_locale_getstring (IDS_GROUP_COMMIT),
		commit_percent_f,
		_r_locale_getstring (IDS_GROUP_3),
		mem_info.system_cache.percent_f
	);

	if (!_r_wnd_isvisible (hwnd, FALSE))
		return;

	// physical memory
	_app_setrowpercent (hwnd, 0, mem_info.physical_memory.percent_f);
	_app_setrowbytes (hwnd, 1, mem_info.physical_memory.free_bytes, mem_info.physical_memory.percent);
	_app_setrowbytes (hwnd, 2, mem_info.physical_memory.total_bytes, mem_info.physical_memory.percent);

	// pagefile (all pagefiles)
	_app_setrowpercent (hwnd, 3, mem_info.page_file.percent_f);
	_app_setrowbytes (hwnd, 4, mem_info.page_file.free_bytes, mem_info.page_file.percent);
	_app_setrowbytes (hwnd, 5, mem_info.page_file.total_bytes, mem_info.page_file.percent);

	// system cache
	_app_setrowpercent (hwnd, 6, mem_info.system_cache.percent_f);
	_app_setrowbytes (hwnd, 7, mem_info.system_cache.free_bytes, mem_info.system_cache.percent);
	_app_setrowbytes (hwnd, 8, mem_info.system_cache.total_bytes, mem_info.system_cache.percent);

	// commit charge ("virtual memory" in 3.4 and Task Manager "Committed")
	_app_setrowpercent (hwnd, LV_ROW_COMMIT, commit_percent_f);
	_app_setrowbytes (hwnd, LV_ROW_COMMIT + 1, (mem_ex.commit_limit > mem_ex.commit_used) ? (mem_ex.commit_limit - mem_ex.commit_used) : 0, commit_percent);
	_app_setrowbytes (hwnd, LV_ROW_COMMIT + 2, mem_ex.commit_limit, commit_percent);

	// memory lists
	if (mem_ex.is_lists_valid)
	{
		_app_setrowbytes (hwnd, LV_ROW_LISTS, mem_ex.standby_bytes, 0);
		_app_setrowbytes (hwnd, LV_ROW_LISTS + 1, mem_ex.modified_bytes, 0);
		_app_setrowbytes (hwnd, LV_ROW_LISTS + 2, mem_ex.free_bytes, 0);
	}
	else
	{
		for (INT i = 0; i < 3; i++)
			_app_setrow (hwnd, LV_ROW_LISTS + i, L"n/a", 0);
	}

	_app_updatepagefilerows (hwnd, &mem_ex);

	_r_listview_redraw (hwnd, IDC_LISTVIEW);
}

VOID _app_iconredraw (
	_In_opt_ HWND hwnd
)
{
	config.ms_prev = 0;

	if (hwnd)
		_app_timercallback (hwnd, 0, UID, 0);
}

VOID _app_iconinit (
	_In_ LONG dpi_value
)
{
	LOGFONT logfont;
	PVOID bits;
	HDC hdc;

	SAFE_DELETE_OBJECT (config.hbitmap_mask);
	SAFE_DELETE_OBJECT (config.hbitmap);
	SAFE_DELETE_OBJECT (config.hfont);

	SAFE_DELETE_DC (config.hdc_mask);
	SAFE_DELETE_DC (config.hdc);

	// init font
	_app_fontinit (&logfont, dpi_value);

	config.hfont = CreateFontIndirectW (&logfont);

	// init rect
	SetRect (&config.icon_size, 0, 0, _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value), _r_dc_getsystemmetrics (SM_CYSMICON, dpi_value));

	// init dc
	hdc = GetDC (NULL);

	if (!hdc)
		return;

	config.hdc = CreateCompatibleDC (hdc);
	config.hdc_mask = CreateCompatibleDC (hdc);

	// init bitmap
	config.hbitmap = _r_dc_createbitmap (hdc, config.icon_size.right, config.icon_size.bottom, &bits);
	config.hbitmap_mask = CreateBitmap (config.icon_size.right, config.icon_size.bottom, 1, 1, NULL);

	ReleaseDC (NULL, hdc);
}

VOID _app_hotkeyinit (
	_In_ HWND hwnd
)
{
	LONG hotkey;

	UnregisterHotKey (hwnd, UID);

	if (!_r_config_getboolean_ex (L"HotkeyCleanEnable", FALSE, NULL))
		return;

	hotkey = _r_config_getlong_ex (L"HotkeyClean", MAKEWORD (VK_F1, HOTKEYF_CONTROL), NULL);

	if (!hotkey)
		return;

	if (!RegisterHotKey (hwnd, UID, (HIBYTE (hotkey) & 2) | ((HIBYTE (hotkey) & 4) >> 2) | ((HIBYTE (hotkey) & 1) << 2), LOBYTE (hotkey)))
		_r_show_errormessage (hwnd, L"Could not register hotkey!", NtLastError (), NULL, ET_WINDOWS);
}

FORCEINLINE VOID _app_setfontcontrol (
	_In_ HWND hwnd,
	_In_ INT ctrl_id,
	_In_ PLOGFONT logfont,
	_In_ LONG dpi_value
)
{
	_r_ctrl_setstringformat (hwnd, IDC_FONT, L"%s, %" TEXT (PR_LONG) L"px, %" TEXT (PR_LONG), logfont->lfFaceName, _r_dc_fontheighttosize (logfont->lfHeight, dpi_value), logfont->lfWeight);
}

INT_PTR CALLBACK SettingsProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case RM_INITIALIZE:
		{
			INT dialog_id = (INT)wparam;

			switch (dialog_id)
			{
				case IDD_SETTINGS_GENERAL:
				{
					_r_ctrl_checkbutton (hwnd, IDC_ALWAYSONTOP_CHK, _r_config_getboolean_ex (L"AlwaysOnTop", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_LOADONSTARTUP_CHK, _r_autorun_isenabled ());
					_r_ctrl_checkbutton (hwnd, IDC_STARTMINIMIZED_CHK, _r_config_getboolean_ex (L"IsStartMinimized", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_REDUCTCONFIRMATION_CHK, _r_config_getboolean_ex (L"IsShowReductConfirmation", TRUE, NULL));

					if (!_r_sys_iselevated ())
						_r_ctrl_enable (hwnd, IDC_SKIPUACWARNING_CHK, FALSE);

					_r_ctrl_checkbutton (hwnd, IDC_SKIPUACWARNING_CHK, _r_skipuac_isenabled ());
					_r_ctrl_checkbutton (hwnd, IDC_CHECKUPDATES_CHK, _r_update_isenabled (FALSE));

					_r_locale_enum (hwnd, IDC_LANGUAGE, 0);

					break;
				}

				case IDD_SETTINGS_MEMORY:
				{
					WCHAR buffer[0x100];
					ULONG mask;

					_r_listview_setstyle (hwnd, IDC_REGIONS, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER, FALSE);

					_r_wnd_setcontext (hwnd, IDC_REGIONS, INVALID_HANDLE_VALUE);

					// fix by @XakerTwo
					_r_listview_deleteallcolumns (hwnd, IDC_REGIONS);
					_r_listview_deleteallitems (hwnd, IDC_REGIONS);

					_r_listview_addcolumn (hwnd, IDC_REGIONS, 0, NULL, 10, LVCFMT_LEFT);

					_r_listview_additem (hwnd, IDC_REGIONS, 0, _r_locale_getstring (IDS_MEMREGION_WORKINGSET), I_DEFAULT, I_DEFAULT, REDUCT_WORKINGSET);
					_r_listview_additem (hwnd, IDC_REGIONS, 1, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE), I_DEFAULT, I_DEFAULT, REDUCT_SYSTEMFILECACHE);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
					_r_listview_additem (hwnd, IDC_REGIONS, 2, buffer, I_DEFAULT, I_DEFAULT, REDUCT_MODIFIEDLIST);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
					_r_listview_additem (hwnd, IDC_REGIONS, 3, _r_locale_getstring (IDS_MEMREGION_STANDBYLIST), I_DEFAULT, I_DEFAULT, REDUCT_STANDBYLIST);

					_r_listview_additem (hwnd, IDC_REGIONS, 4, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0), I_DEFAULT, I_DEFAULT, REDUCT_STANDBYPRIORITY0LIST);
					_r_listview_additem (hwnd, IDC_REGIONS, 5, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE), I_DEFAULT, I_DEFAULT, REDUCT_MODIFIEDFILECACHE);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s (win8.1+)", _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
					_r_listview_additem (hwnd, IDC_REGIONS, 6, buffer, I_DEFAULT, I_DEFAULT, REDUCT_REGISTRYCACHE);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s (win10+)", _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));
					_r_listview_additem (hwnd, IDC_REGIONS, 7, buffer, I_DEFAULT, I_DEFAULT, REDUCT_COMBINEMEMORYLISTS);

					_r_listview_setcolumn (hwnd, IDC_REGIONS, 0, NULL, -100);

					mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 0, (mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 1, (mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 2, (mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 3, (mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 4, (mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 5, (mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 6, (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE);
					_r_listview_setitemcheck (hwnd, IDC_REGIONS, 7, (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS);

					_r_wnd_removecontext (hwnd, IDC_REGIONS);

					if (!_r_sys_iselevated ())
					{
						_r_ctrl_enable (hwnd, IDC_REGIONS, FALSE);
						_r_ctrl_enable (hwnd, IDC_AUTOREDUCTENABLE_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, FALSE);
					}

					_r_ctrl_checkbutton (hwnd, IDC_AUTOREDUCTENABLE_CHK, _r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL));

					_r_updown_setrange (hwnd, IDC_AUTOREDUCTVALUE, 0, 100);

					_r_updown_setvalue (hwnd, IDC_AUTOREDUCTVALUE, _app_getlimitvalue ());

					_r_ctrl_checkbutton (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, _r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL));

					_r_updown_setrange (hwnd, IDC_AUTOREDUCTINTERVALVALUE, 1, 1440);

					_r_updown_setvalue (hwnd, IDC_AUTOREDUCTINTERVALVALUE, _app_getintervalvalue ());

					_r_ctrl_checkbutton (hwnd, IDC_HOTKEY_CLEAN_CHK, _r_config_getboolean_ex (L"HotkeyCleanEnable", FALSE, NULL));

					if (!_r_ctrl_isbuttonchecked (hwnd, IDC_HOTKEY_CLEAN_CHK))
						_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, FALSE);

					_r_hotkey_set (hwnd, IDC_HOTKEY_CLEAN, _r_config_getlong_ex (L"HotkeyClean", MAKEWORD (VK_F1, HOTKEYF_CONTROL), NULL));

					_r_ctrl_sendcommand (hwnd, IDC_AUTOREDUCTENABLE_CHK, 0);
					_r_ctrl_sendcommand (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, 0);
					_r_ctrl_sendcommand (hwnd, IDC_HOTKEY_CLEAN_CHK, 0);

					break;
				}

				case IDD_SETTINGS_APPEARANCE:
				{
					LOGFONT logfont;
					LONG dpi_value;

					_r_ctrl_checkbutton (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, _r_config_getboolean_ex (L"TrayUseTransparency", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYSHOWBORDER_CHK, _r_config_getboolean_ex (L"TrayShowBorder", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYROUNDCORNERS_CHK, _r_config_getboolean_ex (L"TrayRoundCorners", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYCHANGEBG_CHK, _r_config_getboolean_ex (L"TrayChangeBg", TRUE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_TRAYUSEANTIALIASING_CHK, _r_config_getboolean_ex (L"TrayUseAntialiasing", FALSE, NULL));

					dpi_value = _r_dc_gettaskbardpi ();

					_app_fontinit (&logfont, dpi_value);
					_app_setfontcontrol (hwnd, IDC_FONT, &logfont, dpi_value);

					_r_listview_setstyle (hwnd, IDC_COLORS, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP, FALSE);

					// fix by @XakerTwo
					_r_listview_deleteallcolumns (hwnd, IDC_COLORS);
					_r_listview_deleteallitems (hwnd, IDC_COLORS);

					_r_listview_addcolumn (hwnd, IDC_COLORS, 0, NULL, -100, LVCFMT_LEFT);

					_r_listview_additem (hwnd, IDC_COLORS, 0, _r_locale_getstring (IDS_COLOR_TEXT_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorText", TRAY_COLOR_TEXT, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 1, _r_locale_getstring (IDS_COLOR_BACKGROUND_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorBg", TRAY_COLOR_BG, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 2, _r_locale_getstring (IDS_COLOR_WARNING_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL));
					_r_listview_additem (hwnd, IDC_COLORS, 3, _r_locale_getstring (IDS_COLOR_DANGER_HINT), I_DEFAULT, I_DEFAULT, _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL));

					break;
				}

				case IDD_SETTINGS_TRAY:
				{
					_r_updown_setrange (hwnd, IDC_TRAYLEVELWARNING, 0, 100);
					_r_updown_setvalue (hwnd, IDC_TRAYLEVELWARNING, _app_getwarningvalue ());

					_r_updown_setrange (hwnd, IDC_TRAYLEVELDANGER, 0, 100);
					_r_updown_setvalue (hwnd, IDC_TRAYLEVELDANGER, _app_getdangervalue ());

					_r_combobox_setcurrentitem (hwnd, IDC_TRAYACTIONSC, _r_config_getlong_ex (L"TrayActionDc", 0, NULL));
					_r_combobox_setcurrentitem (hwnd, IDC_TRAYACTIONMC, _r_config_getlong_ex (L"TrayActionMc", 1, NULL));

					_r_ctrl_checkbutton (hwnd, IDC_SHOW_CLEAN_RESULT_CHK, _r_config_getboolean_ex (L"BalloonCleanResults", TRUE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_NOTIFICATIONSOUND_CHK, _r_config_getboolean_ex (L"IsNotificationsSound", TRUE, NULL));

					break;
				}

				case IDD_SETTINGS_ADVANCED:
				{
					_r_ctrl_checkbutton (hwnd, IDC_ALLOWSTANDBYLISTCLEANUP_CHK, _r_config_getboolean_ex (L"IsAllowStandbyListCleanup", FALSE, NULL));
					_r_ctrl_checkbutton (hwnd, IDC_LOGRESULTS_CHK, _r_config_getboolean_ex (L"LogCleanResults", FALSE, NULL));

					break;
				}
			}

			break;
		}

		case RM_LOCALIZE:
		{
			INT dialog_id = (INT)wparam;

			// localize titles
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_1, L"%s:", _r_locale_getstring (IDS_TITLE_1));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_2, L"%s: (Language)", _r_locale_getstring (IDS_TITLE_2));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_3, L"%s:", _r_locale_getstring (IDS_TITLE_3));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_4, L"%s:", _r_locale_getstring (IDS_TITLE_4));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_5, L"%s:", _r_locale_getstring (IDS_TITLE_5));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_6, L"%s:", _r_locale_getstring (IDS_TITLE_6));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_7, L"%s:", _r_locale_getstring (IDS_TITLE_7));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_8, L"%s:", _r_locale_getstring (IDS_TITLE_8));
			_r_ctrl_setstringformat (hwnd, IDC_TITLE_9, L"%s:", _r_locale_getstring (IDS_TITLE_9));

			switch (dialog_id)
			{
				case IDD_SETTINGS_GENERAL:
				{
					_r_ctrl_setstring (hwnd, IDC_ALWAYSONTOP_CHK, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
					_r_ctrl_setstring (hwnd, IDC_LOADONSTARTUP_CHK, _r_locale_getstring (IDS_LOADONSTARTUP_CHK));
					_r_ctrl_setstring (hwnd, IDC_STARTMINIMIZED_CHK, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
					_r_ctrl_setstring (hwnd, IDC_REDUCTCONFIRMATION_CHK, _r_locale_getstring (IDS_REDUCTCONFIRMATION_CHK));
					_r_ctrl_setstring (hwnd, IDC_SKIPUACWARNING_CHK, _r_locale_getstring (IDS_SKIPUACWARNING_CHK));
					_r_ctrl_setstring (hwnd, IDC_CHECKUPDATES_CHK, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
					_r_ctrl_setstring (hwnd, IDC_LANGUAGE_HINT, _r_locale_getstring (IDS_LANGUAGE_HINT));

					break;
				}

				case IDD_SETTINGS_MEMORY:
				{
					_r_ctrl_setstring (hwnd, IDC_AUTOREDUCTENABLE_CHK, _r_locale_getstring (IDS_AUTOREDUCTENABLE_CHK));
					_r_ctrl_setstring (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, _r_locale_getstring (IDS_AUTOREDUCTINTERVALENABLE_CHK));
					_r_ctrl_setstring (hwnd, IDC_HOTKEY_CLEAN_CHK, _r_locale_getstring (IDS_HOTKEY_CLEAN_CHK));

					break;
				}

				case IDD_SETTINGS_APPEARANCE:
				{
					_r_ctrl_setstring (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, _r_locale_getstring (IDS_TRAYUSETRANSPARENCY_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYSHOWBORDER_CHK, _r_locale_getstring (IDS_TRAYSHOWBORDER_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYROUNDCORNERS_CHK, _r_locale_getstring (IDS_TRAYROUNDCORNERS_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYCHANGEBG_CHK, _r_locale_getstring (IDS_TRAYCHANGEBG_CHK));
					_r_ctrl_setstring (hwnd, IDC_TRAYUSEANTIALIASING_CHK, _r_locale_getstring (IDS_TRAYUSEANTIALIASING_CHK));
					_r_ctrl_setstring (hwnd, IDC_FONT_HINT, _r_locale_getstring (IDS_FONT_HINT));

					_r_listview_setitem (hwnd, IDC_COLORS, 0, 0, _r_locale_getstring (IDS_COLOR_TEXT_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 1, 0, _r_locale_getstring (IDS_COLOR_BACKGROUND_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 2, 0, _r_locale_getstring (IDS_COLOR_WARNING_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);
					_r_listview_setitem (hwnd, IDC_COLORS, 3, 0, _r_locale_getstring (IDS_COLOR_DANGER_HINT), I_DEFAULT, I_DEFAULT, I_DEFAULT);

					break;
				}

				case IDD_SETTINGS_TRAY:
				{
					LPCWSTR string;

					_r_ctrl_setstring (hwnd, IDC_TRAYLEVELWARNING_HINT, _r_locale_getstring (IDS_TRAYLEVELWARNING_HINT));
					_r_ctrl_setstring (hwnd, IDC_TRAYLEVELDANGER_HINT, _r_locale_getstring (IDS_TRAYLEVELDANGER_HINT));

					_r_ctrl_setstring (hwnd, IDC_TRAYACTIONSC_HINT, _r_locale_getstring (IDS_TRAYACTIONSC_HINT));
					_r_ctrl_setstring (hwnd, IDC_TRAYACTIONMC_HINT, _r_locale_getstring (IDS_TRAYACTIONMC_HINT));

					_r_combobox_clear (hwnd, IDC_TRAYACTIONSC);
					_r_combobox_clear (hwnd, IDC_TRAYACTIONMC);

					for (INT i = 0; i < 3; i++)
					{
						string = _r_locale_getstring (IDS_TRAY_ACTION_1 + i);

						_r_combobox_insertitem (hwnd, IDC_TRAYACTIONSC, i, string, i);
						_r_combobox_insertitem (hwnd, IDC_TRAYACTIONMC, i, string, i);
					}

					_r_combobox_setcurrentitembylparam (hwnd, IDC_TRAYACTIONSC, _r_config_getlong_ex (L"TrayActionDc", 0, NULL));
					_r_combobox_setcurrentitembylparam (hwnd, IDC_TRAYACTIONMC, _r_config_getlong_ex (L"TrayActionMc", 1, NULL));

					_r_ctrl_setstring (hwnd, IDC_SHOW_CLEAN_RESULT_CHK, _r_locale_getstring (IDS_SHOW_CLEAN_RESULT_CHK));
					_r_ctrl_setstring (hwnd, IDC_NOTIFICATIONSOUND_CHK, _r_locale_getstring (IDS_NOTIFICATIONSOUND_CHK));

					break;
				}

				case IDD_SETTINGS_ADVANCED:
				{
					_r_ctrl_setstring (hwnd, IDC_ALLOWSTANDBYLISTCLEANUP_CHK, _r_locale_getstring (IDS_ALLOWSTANDBYLISTCLEANUP_CHK));
					_r_ctrl_setstring (hwnd, IDC_LOGRESULTS_CHK, _r_locale_getstring (IDS_LOGRESULTS_CHK));

					break;
				}
			}

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;
					COLORREF clr;

					switch (lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);

							return CDRF_NOTIFYITEMDRAW;
						}

						case CDDS_ITEMPREPAINT:
						{
							LONG_PTR value = CDRF_DODEFAULT;

							if (lpnmlv->nmcd.hdr.idFrom == IDC_REGIONS)
							{
								if (_r_sys_isosversionlower (WINDOWS_8_1) && (lpnmlv->nmcd.lItemlParam & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
								{
									clr = _r_theme_isenabled () ? WND_GRAYTEXT_CLR : GetSysColor (COLOR_GRAYTEXT);

									lpnmlv->clrText = _r_dc_getcolorbrightness (clr);
									lpnmlv->clrTextBk = clr;

									value = CDRF_NEWFONT;
								}

								if (_r_sys_isosversionlower (WINDOWS_10) && (lpnmlv->nmcd.lItemlParam & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
								{
									clr = _r_theme_isenabled () ? WND_GRAYTEXT_CLR : GetSysColor (COLOR_GRAYTEXT);

									lpnmlv->clrText = _r_dc_getcolorbrightness (clr);
									lpnmlv->clrTextBk = clr;

									value = CDRF_NEWFONT;
								}
							}
							else if (lpnmlv->nmcd.hdr.idFrom == IDC_COLORS)
							{
								clr = (COLORREF)lpnmlv->nmcd.lItemlParam;

								lpnmlv->clrText = _r_dc_getcolorbrightness (clr);
								lpnmlv->clrTextBk = clr;

								_r_dc_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, clr);

								value = CDRF_NEWFONT;
							}

							SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, value);

							return value;
						}
					}

					break;
				}

				case NM_DBLCLK:
				{
					LPNMITEMACTIVATE lpnmlv = (LPNMITEMACTIVATE)lparam;
					CHOOSECOLOR cc = {0};
					COLORREF clr, cust[0x10] = {TRAY_COLOR_DANGER, TRAY_COLOR_WARNING, TRAY_COLOR_BG, TRAY_COLOR_TEXT};

					if (lpnmlv->hdr.idFrom != IDC_COLORS || lpnmlv->iItem == INT_ERROR)
						break;

					clr = (COLORREF)_r_listview_getitemlparam (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem);

					cc.lStructSize = sizeof (CHOOSECOLOR);
					cc.Flags = CC_RGBINIT | CC_FULLOPEN;
					cc.hwndOwner = hwnd;
					cc.lpCustColors = cust;
					cc.rgbResult = clr;

					if (ChooseColorW (&cc))
					{
						if (lpnmlv->iItem == 0)
						{
							_r_config_setulong_ex (L"TrayColorText", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 1)
						{
							_r_config_setulong_ex (L"TrayColorBg", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 2)
						{
							_r_config_setulong_ex (L"TrayColorWarning", cc.rgbResult, NULL);
						}
						else if (lpnmlv->iItem == 3)
						{
							_r_config_setulong_ex (L"TrayColorDanger", cc.rgbResult, NULL);
						}

						_r_listview_setitem (hwnd, IDC_COLORS, lpnmlv->iItem, lpnmlv->iSubItem, NULL, I_DEFAULT, I_DEFAULT, cc.rgbResult);

						_r_listview_redraw (hwnd, IDC_COLORS);

						_app_iconinit (_r_dc_gettaskbardpi ());
						_app_iconredraw (_r_app_gethwnd ());
					}

					break;
				}

				case LVN_ITEMCHANGED:
				{
					LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)lparam;
					ULONG mask, value;

					if (lpnmlv->hdr.idFrom != IDC_REGIONS)
						break;

					if (_r_wnd_getcontext (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom))
						break;

					if ((lpnmlv->uChanged & LVIF_STATE) == 0)
						break;

					if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (1) || ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2)))
					{
						value = (ULONG)lpnmlv->lParam;

						mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

						if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2))
						{
							if ((value & REDUCT_MASK_FREEZES) != 0)
							{
								if (!_r_show_confirmmessage (hwnd, _r_locale_getstring (IDS_QUESTION_WARNING), NULL, L"IsShowWarningConfirmation", FALSE))
								{
									_r_listview_setitemcheck (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem, FALSE);

									return FALSE;
								}
							}
						}

						if (_r_sys_isosversionlower (WINDOWS_8_1) && (value & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE)
						{
							_r_listview_setitemcheck (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem, (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE);

							return FALSE;
						}

						if (_r_sys_isosversionlower (WINDOWS_10) && (value & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS)
						{
							_r_listview_setitemcheck (hwnd, (INT)(INT_PTR)lpnmlv->hdr.idFrom, lpnmlv->iItem, (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS);

							return FALSE;
						}

						if ((lpnmlv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK (2))
						{
							mask |= value;
						}
						else
						{
							mask &= ~value;
						}

						_r_config_setulong_ex (L"ReductMask2", mask, NULL);
					}

					break;
				}
			}

			break;
		}

		case WM_VSCROLL:
		case WM_HSCROLL:
		{
			LONG value;
			INT ctrl_id;
			BOOLEAN is_stylechanged = FALSE;

			ctrl_id = GetDlgCtrlID ((HWND)lparam);

			if (ctrl_id == IDC_AUTOREDUCTVALUE)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"AutoreductValue", value, NULL);
			}
			else if (ctrl_id == IDC_AUTOREDUCTINTERVALVALUE)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"AutoreductIntervalValue", value, NULL);
			}
			else if (ctrl_id == IDC_TRAYLEVELWARNING)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"TrayLevelWarning", value, NULL);

				is_stylechanged = TRUE;
			}
			else if (ctrl_id == IDC_TRAYLEVELDANGER)
			{
				value = _r_updown_getvalue (hwnd, ctrl_id);

				_r_config_setlong_ex (L"TrayLevelDanger", value, NULL);

				is_stylechanged = TRUE;
			}

			if (is_stylechanged)
			{
				_app_iconredraw (_r_app_gethwnd ());

				_r_listview_redraw (_r_app_gethwnd (), IDC_LISTVIEW);
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			switch (ctrl_id)
			{
				case IDC_AUTOREDUCTVALUE_CTRL:
				{
					LONG value;

					if (notify_code == EN_CHANGE)
					{
						value = _r_updown_getvalue (hwnd, IDC_AUTOREDUCTVALUE);

						_r_config_setlong_ex (L"AutoreductValue", value, NULL);
					}

					break;
				}

				case IDC_AUTOREDUCTINTERVALVALUE_CTRL:
				{
					LONG value;

					if (notify_code == EN_CHANGE)
					{
						value = _r_updown_getvalue (hwnd, IDC_AUTOREDUCTINTERVALVALUE);

						_r_config_setlong_ex (L"AutoreductIntervalValue", value, NULL);
					}

					break;
				}

				case IDC_TRAYLEVELWARNING_CTRL:
				case IDC_TRAYLEVELDANGER_CTRL:
				{
					if (notify_code == EN_CHANGE)
					{
						if (ctrl_id == IDC_TRAYLEVELWARNING_CTRL)
						{
							_r_config_setlong_ex (L"TrayLevelWarning", _r_updown_getvalue (hwnd, IDC_TRAYLEVELWARNING), NULL);
						}
						else if (ctrl_id == IDC_TRAYLEVELDANGER_CTRL)
						{
							_r_config_setlong_ex (L"TrayLevelDanger", _r_updown_getvalue (hwnd, IDC_TRAYLEVELDANGER), NULL);
						}

						_app_iconredraw (_r_app_gethwnd ());

						_r_listview_redraw (_r_app_gethwnd (), IDC_LISTVIEW);
					}

					break;
				}

				case IDC_ALWAYSONTOP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"AlwaysOnTop", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_LOADONSTARTUP_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_autorun_enable (hwnd, is_enable);

					is_enable = _r_autorun_isenabled ();

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_LOADONSTARTUP_CHK, 0, MF_BYCOMMAND, is_enable);

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_STARTMINIMIZED_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsStartMinimized", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_REDUCTCONFIRMATION_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_config_setboolean_ex (L"IsShowReductConfirmation", is_enable, NULL);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_REDUCTCONFIRMATION_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_SKIPUACWARNING_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_skipuac_enable (hwnd, is_enable);

					is_enable = _r_skipuac_isenabled ();

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_SKIPUACWARNING_CHK, 0, MF_BYCOMMAND, is_enable);

					_r_ctrl_checkbutton (hwnd, ctrl_id, is_enable);

					break;
				}

				case IDC_CHECKUPDATES_CHK:
				{
					BOOLEAN is_enable = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_update_enable (is_enable);

					is_enable = _r_update_isenabled (FALSE);

					_r_menu_checkitem (GetMenu (_r_app_gethwnd ()), IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, is_enable);

					break;
				}

				case IDC_LANGUAGE:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_locale_apply (hwnd, ctrl_id, 0);

					break;
				}

				case IDC_AUTOREDUCTENABLE_CHK:
				{
					HWND hbuddy;
					BOOLEAN is_enabled = _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_enabled);

					if (is_enabled)
						_r_config_setboolean_ex (L"AutoreductEnable", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_AUTOREDUCTINTERVALENABLE_CHK:
				{
					HWND hbuddy;
					BOOLEAN is_enabled = _r_ctrl_isenabled (hwnd, ctrl_id);

					hbuddy = _r_updown_getbuddy (hwnd, IDC_AUTOREDUCTINTERVALVALUE);

					if (hbuddy)
						_r_ctrl_enable (hbuddy, 0, is_enabled);

					if (is_enabled)
						_r_config_setboolean_ex (L"AutoreductIntervalEnable", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_HOTKEY_CLEAN_CHK:
				{
					BOOLEAN is_checked = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_ctrl_enable (hwnd, IDC_HOTKEY_CLEAN, is_checked);

					_r_config_setboolean_ex (L"HotkeyCleanEnable", is_checked, NULL);

					_app_hotkeyinit (_r_app_gethwnd ());

					break;
				}

				case IDC_HOTKEY_CLEAN:
				{
					if (!_r_ctrl_isbuttonchecked (hwnd, IDC_HOTKEY_CLEAN_CHK))
						break;

					if (notify_code == EN_CHANGE)
					{
						_r_config_setlong_ex (L"HotkeyClean", _r_hotkey_get (hwnd, ctrl_id), NULL);

						_app_hotkeyinit (_r_app_gethwnd ());
					}

					break;
				}

				case IDC_TRAYUSETRANSPARENCY_CHK:
				case IDC_TRAYSHOWBORDER_CHK:
				case IDC_TRAYROUNDCORNERS_CHK:
				case IDC_TRAYCHANGEBG_CHK:
				case IDC_TRAYUSEANTIALIASING_CHK:
				{
					BOOLEAN is_enabled = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					switch (ctrl_id)
					{
						case IDC_TRAYUSETRANSPARENCY_CHK:
						{
							_r_config_setboolean_ex (L"TrayUseTransparency", is_enabled, NULL);
							break;
						}

						case IDC_TRAYSHOWBORDER_CHK:
						{
							_r_config_setboolean_ex (L"TrayShowBorder", is_enabled, NULL);
							break;
						}

						case IDC_TRAYROUNDCORNERS_CHK:
						{
							_r_config_setboolean_ex (L"TrayRoundCorners", is_enabled, NULL);
							break;
						}

						case IDC_TRAYCHANGEBG_CHK:
						{
							_r_config_setboolean_ex (L"TrayChangeBg", is_enabled, NULL);
							break;
						}

						case IDC_TRAYUSEANTIALIASING_CHK:
						{
							_r_config_setboolean_ex (L"TrayUseAntialiasing", is_enabled, NULL);
							break;
						}
					}

					_app_iconinit (_r_dc_gettaskbardpi ());
					_app_iconredraw (_r_app_gethwnd ());

					break;
				}

				case IDC_TRAYACTIONSC:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_config_setlong_ex (L"TrayActionDc", _r_combobox_getcurrentitem (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_TRAYACTIONMC:
				{
					if (notify_code == CBN_SELCHANGE)
						_r_config_setlong_ex (L"TrayActionMc", _r_combobox_getcurrentitem (hwnd, ctrl_id), NULL);

					break;
				}

				case IDC_SHOW_CLEAN_RESULT_CHK:
				{
					_r_config_setboolean_ex (L"BalloonCleanResults", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_NOTIFICATIONSOUND_CHK:
				{
					_r_config_setboolean_ex (L"IsNotificationsSound", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_FONT:
				{
					CHOOSEFONT cf = {0};
					LOGFONT logfont;
					LONG dpi_value;

					cf.lStructSize = sizeof (CHOOSEFONT);
					cf.hwndOwner = hwnd;
					cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_SCREENFONTS;

					dpi_value = _r_dc_gettaskbardpi ();

					_app_fontinit (&logfont, dpi_value);

					cf.lpLogFont = &logfont;

					if (ChooseFontW (&cf))
					{
						_r_config_setfont_ex (L"TrayFont", &logfont, dpi_value, NULL);

						_app_setfontcontrol (hwnd, IDC_FONT, &logfont, dpi_value);

						_app_iconinit (dpi_value);
						_app_iconredraw (_r_app_gethwnd ());
					}

					break;
				}

				case IDC_ALLOWSTANDBYLISTCLEANUP_CHK:
				{
					_r_config_setboolean_ex (L"IsAllowStandbyListCleanup", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}

				case IDC_LOGRESULTS_CHK:
				{
					_r_config_setboolean_ex (L"LogCleanResults", _r_ctrl_isbuttonchecked (hwnd, ctrl_id), NULL);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

typedef struct _APP_PAGEFILE_CONSUMER
{
	ULONG64 bytes;
	ULONG pid;
	WCHAR name[64];
} APP_PAGEFILE_CONSUMER, *PAPP_PAGEFILE_CONSUMER;

#define APP_TOP_CONSUMERS 12

// Who holds the pagefile: per pagefile usage, commit charge, and the processes with the
// most private memory that is not in RAM (paged out to the pagefile or compressed).
VOID _app_showpagefilereport (
	_In_opt_ HWND hwnd
)
{
	APP_PAGEFILE_CONSUMER top[APP_TOP_CONSUMERS] = {0};
	WCHAR a[0x40], b[0x40], c[0x40], d[0x40];
	PSYSTEM_PROCESS_INFORMATION processes;
	PSYSTEM_PROCESS_INFORMATION process;
	APP_MEMORY_EX mem_ex;
	PAPP_PAGEFILE pagefile;
	ULONG64 private_bytes, resident_bytes, bytes;
	ULONG name_length;
	ULONG count = 0;
	ULONG pos;
	PWCHAR buffer;
	ULONG_PTR buffer_length = 0x2000;

	_app_getmemoryinfo_ex (&mem_ex);

	buffer = _r_mem_allocate (buffer_length * sizeof (WCHAR));

	_r_str_append (buffer, buffer_length, L"Pagefiles:\r\n");

	if (!mem_ex.pagefile_count)
		_r_str_append (buffer, buffer_length, L"  (no pagefile)\r\n");

	for (ULONG i = 0; i < mem_ex.pagefile_count; i++)
	{
		pagefile = &mem_ex.pagefiles[i];

		_r_format_bytesize64 (a, RTL_NUMBER_OF (a), pagefile->used_bytes);
		_r_format_bytesize64 (b, RTL_NUMBER_OF (b), pagefile->total_bytes);
		_r_format_bytesize64 (c, RTL_NUMBER_OF (c), pagefile->peak_bytes);

		_r_str_appendformat (buffer, buffer_length, L"  %s: %s in use of %s (peak %s)", pagefile->name, a, b, c);

		if (pagefile->max_bytes)
		{
			_r_format_bytesize64 (a, RTL_NUMBER_OF (a), pagefile->min_bytes);
			_r_format_bytesize64 (b, RTL_NUMBER_OF (b), pagefile->max_bytes);

			_r_str_appendformat (buffer, buffer_length, L", size %s - %s", a, b);
		}

		_r_str_append (buffer, buffer_length, L"\r\n");
	}

	_r_format_bytesize64 (a, RTL_NUMBER_OF (a), mem_ex.commit_used);
	_r_format_bytesize64 (b, RTL_NUMBER_OF (b), mem_ex.commit_limit);
	_r_format_bytesize64 (c, RTL_NUMBER_OF (c), mem_ex.commit_peak);

	_r_str_appendformat (buffer, buffer_length, L"\r\nCommit charge: %s of %s (peak %s)\r\n", a, b, c);

	if (mem_ex.is_lists_valid)
	{
		_r_format_bytesize64 (d, RTL_NUMBER_OF (d), mem_ex.modified_pagefile_bytes);

		_r_str_appendformat (buffer, buffer_length, L"Modified pages waiting to be written to the pagefile: %s\r\n", d);
	}

	processes = _app_queryprocesses ();

	if (processes)
	{
		process = processes;

		while (TRUE)
		{
			private_bytes = process->PrivatePageCount;
			resident_bytes = (ULONG64)process->WorkingSetPrivateSize.QuadPart;

			// Includes committed pages never touched, not just pagefile contents.
			bytes = (private_bytes > resident_bytes) ? (private_bytes - resident_bytes) : 0;

			if (HandleToUlong (process->UniqueProcessId) && bytes)
			{
				// insertion into a small sorted array
				pos = count;

				while (pos > 0 && top[pos - 1].bytes < bytes)
				{
					if (pos < APP_TOP_CONSUMERS)
						top[pos] = top[pos - 1];

					pos -= 1;
				}

				if (pos < APP_TOP_CONSUMERS)
				{
					top[pos].bytes = bytes;
					top[pos].pid = HandleToUlong (process->UniqueProcessId);

					name_length = min (process->ImageName.Length / sizeof (WCHAR), RTL_NUMBER_OF (top[pos].name) - 1);

					if (process->ImageName.Buffer)
						RtlCopyMemory (top[pos].name, process->ImageName.Buffer, name_length * sizeof (WCHAR));

					top[pos].name[name_length] = UNICODE_NULL;

					if (count < APP_TOP_CONSUMERS)
						count += 1;
				}
			}

			if (!process->NextEntryOffset)
				break;

			process = PTR_ADD_OFFSET (process, process->NextEntryOffset);
		}

		_r_mem_free (processes);
	}

	_r_str_append (buffer, buffer_length, L"\r\nPrivate commit not in RAM (pagefile, compressed, or never touched), by process:\r\n");

	for (ULONG i = 0; i < count; i++)
	{
		_r_format_bytesize64 (a, RTL_NUMBER_OF (a), top[i].bytes);

		_r_str_appendformat (buffer, buffer_length, L"  %s  %s (%" TEXT (PR_ULONG) L")\r\n", a, top[i].name, top[i].pid);
	}

	_r_str_append (
		buffer,
		buffer_length,
		L"\r\nThese amounts are upper bounds on what each process could have in the pagefile, "
		L"not measurements. Only the owning process can release its pagefile space by "
		L"freeing or modifying (writing to) that memory, or exiting. "
		L"\"Wipe pagefile at shutdown\" zeroes the file at every shutdown."
	);

	_r_show_message (hwnd, MB_OK | MB_ICONINFORMATION, _r_locale_getstring (IDS_PAGEFILE_REPORT), buffer);

	_r_mem_free (buffer);
}

VOID _app_resizecolumns (
	_In_ HWND hwnd
)
{
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, NULL, -50);
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, NULL, -50);
}

VOID _app_initialize (
	_In_opt_ HWND hwnd
)
{
	ULONG privileges[] = {
		SE_PROF_SINGLE_PROCESS_PRIVILEGE,
		SE_INCREASE_QUOTA_PRIVILEGE,
	};

	if (_r_sys_iselevated ())
	{
		_r_sys_setprocessprivilege (NtCurrentProcess (), privileges, RTL_NUMBER_OF (privileges), TRUE);
	}
	else
	{
		if (hwnd)
			_r_ctrl_setbuttonshield (hwnd, IDC_CLEAN, TRUE);
	}

	if (!hwnd)
		return;

	_r_ctrl_setbuttonmargins (hwnd, IDC_CLEAN, _r_dc_getwindowdpi (hwnd));

	// configure listview
	_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER, TRUE);

	_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 1, NULL, 10, LVCFMT_RIGHT);
	_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 2, NULL, 10, LVCFMT_LEFT);

	_app_resizecolumns (hwnd);

	// configure listview (pagefile rows are added by the timer)
	for (INT i = 0; i < RTL_NUMBER_OF (app_group_titles); i++)
		_r_listview_addgroup (hwnd, IDC_LISTVIEW, i, _r_locale_getstring (app_group_titles[i]), 0, LVGS_COLLAPSIBLE, LVGS_COLLAPSIBLE);

	for (INT i = 0; i < RTL_NUMBER_OF (app_row_labels); i++)
		_r_listview_additem (hwnd, IDC_LISTVIEW, i, _r_locale_getstring (app_row_labels[i]), I_DEFAULT, i / 3, 0);

	config.pagefile_rows = 0;

	// settings
	_r_settings_addpage (IDD_SETTINGS_GENERAL, IDS_SETTINGS_GENERAL);
	_r_settings_addpage (IDD_SETTINGS_MEMORY, IDS_SETTINGS_MEMORY);
	_r_settings_addpage (IDD_SETTINGS_APPEARANCE, IDS_SETTINGS_APPEARANCE);
	_r_settings_addpage (IDD_SETTINGS_TRAY, IDS_SETTINGS_TRAY);
	_r_settings_addpage (IDD_SETTINGS_ADVANCED, IDS_TITLE_ADVANCED);
}

INT_PTR CALLBACK DlgProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			_r_app_sethwnd (hwnd); // HACK!!!

			_app_initialize (hwnd);

			SetTimer (hwnd, UID, TIMER, &_app_timercallback);

			break;
		}

		case WM_DESTROY:
		{
			KillTimer (hwnd, UID);

			_r_tray_destroy (hwnd, &GUID_TrayIcon);

			PostQuitMessage (0);

			break;
		}

		case RM_INITIALIZE:
		{
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_checkitem (hmenu, IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"AlwaysOnTop", FALSE, NULL));
				_r_menu_checkitem (hmenu, IDM_USEDARKTHEME, 0, MF_BYCOMMAND, _r_theme_isenabled ());
				_r_menu_checkitem (hmenu, IDM_LOADONSTARTUP_CHK, 0, MF_BYCOMMAND, _r_autorun_isenabled ());
				_r_menu_checkitem (hmenu, IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsStartMinimized", FALSE, NULL));
				_r_menu_checkitem (hmenu, IDM_REDUCTCONFIRMATION_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"IsShowReductConfirmation", TRUE, NULL));
				_r_menu_checkitem (hmenu, IDM_SKIPUACWARNING_CHK, 0, MF_BYCOMMAND, _r_skipuac_isenabled ());
				_r_menu_checkitem (hmenu, IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, _r_update_isenabled (FALSE));
				_r_menu_checkitem (hmenu, IDM_SMARTSTANDBY_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"SmartStandbyEnable", FALSE, NULL));
				_r_menu_checkitem (hmenu, IDM_PAGEFILEGUARD_CHK, 0, MF_BYCOMMAND, _r_config_getboolean_ex (L"PagefileGuardEnable", TRUE, NULL));
				_r_menu_checkitem (hmenu, IDM_CLEARPAGEFILE_CHK, 0, MF_BYCOMMAND, _app_isclearpagefileenabled ());

				if (!_r_sys_iselevated ())
				{
					_r_menu_enableitem (hmenu, IDM_SKIPUACWARNING_CHK, FALSE, FALSE);
					_r_menu_enableitem (hmenu, IDM_CLEARPAGEFILE_CHK, FALSE, FALSE);
				}
			}

			_app_iconinit (_r_dc_gettaskbardpi ());

			_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, _app_iconcreate (0), _r_app_getname (), FALSE);

			_app_iconredraw (hwnd);

			break;
		}

		case RM_INITIALIZE_POST:
		{
			if (_r_sys_iselevated ())
				_app_hotkeyinit (hwnd);

			break;
		}

		case RM_TASKBARCREATED:
		{
			_app_iconinit (_r_dc_gettaskbardpi ());

			_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, _app_iconcreate (0), _r_app_getname (), FALSE);

			_app_iconredraw (hwnd);

			break;
		}

		case RM_LOCALIZE:
		{
			// localize menu
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_setitemtext (hmenu, 0, TRUE, _r_locale_getstring (IDS_FILE));
				_r_menu_setitemtext (hmenu, 1, TRUE, _r_locale_getstring (IDS_VIEW));
				_r_menu_setitemtext (hmenu, 2, TRUE, _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, 3, TRUE, _r_locale_getstring (IDS_HELP));

				_r_menu_setitemtextformat (hmenu, IDM_SETTINGS, FALSE, L"%s...\tF2", _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, IDM_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));
				_r_menu_setitemtext (hmenu, IDM_ALWAYSONTOP_CHK, FALSE, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
				_r_menu_setitemtext (hmenu, IDM_LOADONSTARTUP_CHK, FALSE, _r_locale_getstring (IDS_LOADONSTARTUP_CHK));
				_r_menu_setitemtext (hmenu, IDM_USEDARKTHEME, FALSE, _r_locale_getstring (IDS_USEDARKTHEME));
				_r_menu_setitemtext (hmenu, IDM_STARTMINIMIZED_CHK, FALSE, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
				_r_menu_setitemtext (hmenu, IDM_REDUCTCONFIRMATION_CHK, FALSE, _r_locale_getstring (IDS_REDUCTCONFIRMATION_CHK));
				_r_menu_setitemtext (hmenu, IDM_SKIPUACWARNING_CHK, FALSE, _r_locale_getstring (IDS_SKIPUACWARNING_CHK));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES_CHK, FALSE, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
				_r_menu_setitemtextformat (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU, TRUE, L"%s (Language)", _r_locale_getstring (IDS_LANGUAGE));
				_r_menu_setitemtext (hmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES, FALSE, _r_locale_getstring (IDS_CHECKUPDATES));
				_r_menu_setitemtextformat (hmenu, IDM_ABOUT, FALSE, L"%s\tF1", _r_locale_getstring (IDS_ABOUT));

				_r_menu_setitemtextformat (hmenu, IDM_PAGEFILE_REPORT, FALSE, L"%s...", _r_locale_getstring (IDS_PAGEFILE_REPORT));

				_r_menu_setitemtextformat (
					hmenu,
					IDM_SMARTSTANDBY_CHK,
					FALSE,
					_r_locale_getstring (IDS_SMARTSTANDBY_CHK),
					_r_config_getulong_ex (L"SmartStandbyListMB", DEFAULT_SMART_STANDBY_MB, NULL),
					_r_config_getulong_ex (L"SmartStandbyFreeMB", DEFAULT_SMART_FREE_MB, NULL)
				);

				_r_menu_setitemtextformat (
					hmenu,
					IDM_PAGEFILEGUARD_CHK,
					FALSE,
					_r_locale_getstring (IDS_PAGEFILEGUARD_CHK),
					_r_calc_clamp (_r_config_getulong_ex (L"PagefileGuardPercent", DEFAULT_PAGEFILE_GUARD, NULL), 1, 100)
				);

				_r_menu_setitemtext (hmenu, IDM_CLEARPAGEFILE_CHK, FALSE, _r_locale_getstring (IDS_CLEARPAGEFILE_CHK));
			}

			// configure listview
			for (INT i = 0; i < RTL_NUMBER_OF (app_group_titles); i++)
				_r_listview_setgroup (hwnd, IDC_LISTVIEW, i, _r_locale_getstring (app_group_titles[i]), 0, 0);

			for (INT i = 0; i < RTL_NUMBER_OF (app_row_labels); i++)
				_r_listview_setitem (hwnd, IDC_LISTVIEW, i, 0, _r_locale_getstring (app_row_labels[i]), I_DEFAULT, I_DEFAULT, I_DEFAULT);

			// configure button
			_r_ctrl_setstring (hwnd, IDC_CLEAN, _r_locale_getstring (IDS_CLEAN));

			// enum localizations
			if (hmenu)
				_r_locale_enum (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU, IDX_LANGUAGE);

			break;
		}

		case WM_DPICHANGED:
		{
			_app_iconinit (_r_dc_gettaskbardpi ());
			_app_iconredraw (hwnd);

			_app_resizecolumns (hwnd);

			if (!_r_sys_iselevated ())
				_r_ctrl_setbuttonmargins (hwnd, IDC_CLEAN, LOWORD (wparam));

			break;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc;

			hdc = BeginPaint (hwnd, &ps);

			if (!hdc)
				break;

			_r_dc_drawwindow (hdc, hwnd, TRUE);

			EndPaint (hwnd, &ps);

			break;
		}

		case RM_CLEANDONE:
		{
			_app_memoryclean_finish ((PAPP_CLEAN_CONTEXT)lparam);
			break;
		}

		case WM_HOTKEY:
		{
			if (wparam == UID)
				_app_memoryclean (hwnd, SOURCE_HOTKEY, 0);

			break;
		}

		case WM_SHOWWINDOW:
		{
			if (wparam)
				_app_iconredraw (hwnd);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case BCN_DROPDOWN:
				{
					R_RECTANGLE rectangle;
					WCHAR buffer[0x100];
					RECT rect;
					HMENU hmenu;

					hmenu = CreatePopupMenu ();

					if (!hmenu)
						break;

					_r_menu_additem (hmenu, IDM_CLEAN_WORKINGSET, _r_locale_getstring (IDS_MEMREGION_WORKINGSET));
					_r_menu_additem (hmenu, IDM_CLEAN_SYSTEMFILECACHE, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE));
					_r_menu_additem (hmenu, IDM_CLEAN_MODIFIEDFILECACHE, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE));

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
					_r_menu_additem (hmenu, IDM_CLEAN_MODIFIEDLIST, buffer);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
					_r_menu_additem (hmenu, IDM_CLEAN_STANDBYLIST, _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));

					_r_menu_additem (hmenu, IDM_CLEAN_STANDBYLISTPRIORITY0, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0));

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s (win8.1+)", _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
					_r_menu_additem (hmenu, IDM_CLEAN_REGISTRYCACHE, buffer);

					_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"%s (win10+)", _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));
					_r_menu_additem (hmenu, IDM_CLEAN_COMBINEMEMORYLISTS, buffer);

					if (_r_sys_isosversionlower (WINDOWS_8_1))
						_r_menu_enableitem (hmenu, IDM_CLEAN_REGISTRYCACHE, FALSE, FALSE);

					if (_r_sys_isosversionlower (WINDOWS_10))
						_r_menu_enableitem (hmenu, IDM_CLEAN_COMBINEMEMORYLISTS, FALSE, FALSE);

					if (GetClientRect (nmlp->hwndFrom, &rect))
					{
						ClientToScreen (nmlp->hwndFrom, (PPOINT)&rect);

						_r_wnd_recttorectangle (&rectangle, &rect);
						_r_wnd_adjustrectangletoworkingarea (&rectangle, nmlp->hwndFrom);
						_r_wnd_rectangletorect (&rect, &rectangle);

						_r_menu_popup (hmenu, hwnd, (PPOINT)&rect, TRUE);
					}

					DestroyMenu (hmenu);

					break;
				}

				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;
					LONG_PTR result = CDRF_DODEFAULT;
					ULONG value;

					if (nmlp->idFrom != IDC_LISTVIEW)
						break;

					switch (lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							result = (CDRF_NOTIFYPOSTPAINT | CDRF_NOTIFYITEMDRAW);
							break;
						}

						case CDDS_ITEMPREPAINT:
						{
							value = (ULONG)lpnmlv->nmcd.lItemlParam;

							if (value >= _app_getdangervalue ())
							{
								lpnmlv->clrText = _r_config_getulong_ex (L"TrayColorDanger", TRAY_COLOR_DANGER, NULL);

								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
							}
							else if (value >= _app_getwarningvalue ())
							{
								lpnmlv->clrText = _r_config_getulong_ex (L"TrayColorWarning", TRAY_COLOR_WARNING, NULL);

								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
							}

							break;
						}
					}

					SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, result);

					return result;
				}
			}

			break;
		}

		case RM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_KEYSELECT:
				{
					if (GetForegroundWindow () != hwnd)
						_r_wnd_toggle (hwnd, TRUE);

					break;
				}

				case WM_LBUTTONDOWN:
				case WM_MBUTTONDOWN:
				{
					R_STRINGREF taskmgr_sr = PR_STRINGREF_INIT (L"taskmgr.exe");
					LONG action;

					if (LOWORD (lparam) == WM_MBUTTONDOWN)
					{
						action = _r_config_getlong_ex (L"TrayActionMc", 1, NULL);
					}
					else
					{
						action = _r_config_getlong_ex (L"TrayActionDc", 0, NULL);
					}

					switch (action)
					{
						case 1:
						{
							_app_memoryclean (hwnd, SOURCE_MANUAL, 0);
							break;
						}

						case 2:
						{
							_r_sys_createprocess (taskmgr_sr.buffer, NULL, NULL, FALSE);
							break;
						}

						default:
						{
							_r_wnd_toggle (hwnd, FALSE);
							break;
						}
					}

					SetForegroundWindow (hwnd);

					break;
				}

				case WM_CONTEXTMENU:
				{
					HMENU hmenu, hsubmenu, hsubmenu_interval, hsubmenu_limit, hsubmenu_region;
					ULONG mask;

					SetForegroundWindow (hwnd); // don't touch

					hmenu = LoadMenuW (_r_sys_getimagebase (), MAKEINTRESOURCEW (IDM_TRAY));

					if (!hmenu)
						break;

					hsubmenu = GetSubMenu (hmenu, 0);

					if (!hsubmenu)
						break;

					hsubmenu_region = GetSubMenu (hsubmenu, TRAY_SUBMENU_1);
					hsubmenu_limit = GetSubMenu (hsubmenu, TRAY_SUBMENU_2);
					hsubmenu_interval = GetSubMenu (hsubmenu, TRAY_SUBMENU_3);

					// localize
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_SHOW, FALSE, _r_locale_getstring (IDS_TRAY_SHOW));
					_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_CLEAN, FALSE, L"%s...", _r_locale_getstring (IDS_CLEAN));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_1, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_1));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_2, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_2));
					_r_menu_setitemtext (hsubmenu, TRAY_SUBMENU_3, TRUE, _r_locale_getstring (IDS_TRAY_POPUP_3));
					_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_SETTINGS, FALSE, L"%s...", _r_locale_getstring (IDS_SETTINGS));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_ABOUT, FALSE, _r_locale_getstring (IDS_ABOUT));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));

					// configure submenu #1
					if (hsubmenu_region)
					{
						mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

						_r_menu_setitemtext (hsubmenu_region, IDM_WORKINGSET_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_WORKINGSET));
						_r_menu_setitemtext (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_SYSTEMFILECACHE));
						_r_menu_setitemtext (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_MODIFIEDFILECACHE));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_MODIFIEDLIST_CHK, FALSE, L"%s*", _r_locale_getstring (IDS_MEMREGION_MODIFIEDLIST));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_STANDBYLIST_CHK, FALSE, L"%s*", _r_locale_getstring (IDS_MEMREGION_STANDBYLIST));
						_r_menu_setitemtext (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, FALSE, _r_locale_getstring (IDS_MEMREGION_STANDBYLISTPRIORITY0));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_REGISTRYCACHE_CHK, FALSE, L"%s (win8.1+)", _r_locale_getstring (IDS_MEMREGION_REGISTRYCACHE));
						_r_menu_setitemtextformat (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, FALSE, L"%s (win10+)", _r_locale_getstring (IDS_MEMREGION_COMBINEMEMORYLISTS));

						_r_menu_checkitem (hsubmenu_region, IDM_WORKINGSET_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_WORKINGSET) == REDUCT_WORKINGSET);
						_r_menu_checkitem (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_SYSTEMFILECACHE) == REDUCT_SYSTEMFILECACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_MODIFIEDLIST_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_MODIFIEDLIST) == REDUCT_MODIFIEDLIST);
						_r_menu_checkitem (hsubmenu_region, IDM_STANDBYLIST_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_STANDBYLIST) == REDUCT_STANDBYLIST);
						_r_menu_checkitem (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_STANDBYPRIORITY0LIST) == REDUCT_STANDBYPRIORITY0LIST);
						_r_menu_checkitem (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_MODIFIEDFILECACHE) == REDUCT_MODIFIEDFILECACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_REGISTRYCACHE_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_REGISTRYCACHE) == REDUCT_REGISTRYCACHE);
						_r_menu_checkitem (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, 0, MF_BYCOMMAND, (mask & REDUCT_COMBINEMEMORYLISTS) == REDUCT_COMBINEMEMORYLISTS);

						if (!_r_sys_iselevated ())
						{
							_r_menu_enableitem (hsubmenu_region, IDM_WORKINGSET_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_SYSTEMFILECACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_MODIFIEDFILECACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_MODIFIEDLIST_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_STANDBYLIST_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_STANDBYLISTPRIORITY0_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_REGISTRYCACHE_CHK, FALSE, FALSE);
							_r_menu_enableitem (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, FALSE, FALSE);
						}

						// flush registry cache (win8.1+)
						if (_r_sys_isosversionlower (WINDOWS_8_1))
							_r_menu_enableitem (hsubmenu_region, IDM_REGISTRYCACHE_CHK, FALSE, FALSE);

						// combine memory lists (win10+)
						if (_r_sys_isosversionlower (WINDOWS_10))
							_r_menu_enableitem (hsubmenu_region, IDM_COMBINEMEMORYLISTS_CHK, FALSE, FALSE);
					}

					// configure submenu #2
					if (hsubmenu_limit)
					{
						_app_generate_menu (
							hsubmenu_limit,
							IDX_TRAY_POPUP_1,
							limits_arr,
							RTL_NUMBER_OF (limits_arr),
							L"%" TEXT (PR_ULONG) L"%%",
							_app_getlimitvalue (),
							99,
							_r_config_getboolean_ex (L"AutoreductEnable", FALSE, NULL)
						);
					}

					// configure submenu #3
					if (hsubmenu_interval)
					{
						_app_generate_menu (
							hsubmenu_interval,
							IDX_TRAY_POPUP_2,
							intervals_arr,
							RTL_NUMBER_OF (intervals_arr),
							L"%" TEXT (PR_LONG64) L" min.",
							_app_getintervalvalue (),
							1440,
							_r_config_getboolean_ex (L"AutoreductIntervalEnable", FALSE, NULL)
						);
					}

					_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);

					DestroyMenu (hmenu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			if (notify_code == 0 && ctrl_id >= IDX_LANGUAGE && ctrl_id <= IDX_LANGUAGE + (INT)(INT_PTR)_r_locale_getcount () + 1)
			{
				HMENU hmenu, hsubmenu;

				hmenu = GetMenu (hwnd);

				if (hmenu)
				{
					hsubmenu = GetSubMenu (GetSubMenu (hmenu, LANG_SUBMENU), LANG_MENU);

					if (hsubmenu)
						_r_locale_apply (hsubmenu, ctrl_id, IDX_LANGUAGE);
				}

				return FALSE;
			}
			else if ((ctrl_id >= IDX_TRAY_POPUP_1 && ctrl_id <= IDX_TRAY_POPUP_1 + (INT)RTL_NUMBER_OF (limits_arr) - 1))
			{
				ULONG_PTR idx = (ULONG_PTR)(ctrl_id - IDX_TRAY_POPUP_1);

				_r_config_setboolean_ex (L"AutoreductEnable", TRUE, NULL);
				_r_config_setlong_ex (L"AutoreductValue", limits_arr[idx], NULL);

				return FALSE;
			}
			else if ((ctrl_id >= IDX_TRAY_POPUP_2 && ctrl_id <= IDX_TRAY_POPUP_2 + (INT)RTL_NUMBER_OF (intervals_arr) - 1))
			{
				ULONG_PTR idx = (ULONG_PTR)(ctrl_id - IDX_TRAY_POPUP_2);

				_r_config_setboolean_ex (L"AutoreductIntervalEnable", TRUE, NULL);
				_r_config_setlong_ex (L"AutoreductIntervalValue", intervals_arr[idx], NULL);

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDM_PAGEFILE_REPORT:
				{
					_app_showpagefilereport (hwnd);
					break;
				}

				case IDM_SMARTSTANDBY_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"SmartStandbyEnable", FALSE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_PAGEFILEGUARD_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"PagefileGuardEnable", TRUE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_CLEARPAGEFILE_CHK:
				{
					BOOLEAN new_val = !_app_isclearpagefileenabled ();
					LSTATUS status;

					status = _app_setclearpagefile (new_val);

					if (status != ERROR_SUCCESS)
						_r_show_errormessage (hwnd, NULL, status, L"ClearPageFileAtShutdown", ET_WINDOWS);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, _app_isclearpagefileenabled ());

					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"AlwaysOnTop", FALSE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_STARTMINIMIZED_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"IsStartMinimized", FALSE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_REDUCTCONFIRMATION_CHK:
				{
					BOOLEAN new_val = _app_config_invertboolean (L"IsShowReductConfirmation", TRUE, NULL);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);

					break;
				}

				case IDM_LOADONSTARTUP_CHK:
				{
					_r_autorun_enable (hwnd, !_r_autorun_isenabled ());
					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, _r_autorun_isenabled ());

					break;
				}

				case IDM_USEDARKTHEME:
				{
					BOOLEAN is_enabled = !_r_theme_isenabled ();

					_r_theme_enable (hwnd, is_enabled);
					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, is_enabled);

					break;
				}

				case IDM_SKIPUACWARNING_CHK:
				{
					_r_skipuac_enable (hwnd, !_r_skipuac_isenabled ());
					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, _r_skipuac_isenabled ());

					break;
				}

				case IDM_CHECKUPDATES_CHK:
				{
					BOOLEAN new_val = !_r_update_isenabled (FALSE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_update_enable (new_val);

					break;
				}

				case IDM_WORKINGSET_CHK:
				case IDM_SYSTEMFILECACHE_CHK:
				case IDM_MODIFIEDFILECACHE_CHK:
				case IDM_MODIFIEDLIST_CHK:
				case IDM_STANDBYLIST_CHK:
				case IDM_STANDBYLISTPRIORITY0_CHK:
				case IDM_REGISTRYCACHE_CHK:
				case IDM_COMBINEMEMORYLISTS_CHK:
				{
					ULONG mask, new_mask = 0;

					mask = _r_config_getulong_ex (L"ReductMask2", REDUCT_MASK_DEFAULT, NULL);

					switch (ctrl_id)
					{
						case IDM_WORKINGSET_CHK:
						{
							new_mask = REDUCT_WORKINGSET;
							break;
						}

						case IDM_SYSTEMFILECACHE_CHK:
						{
							new_mask = REDUCT_SYSTEMFILECACHE;
							break;
						}

						case IDM_MODIFIEDFILECACHE_CHK:
						{
							new_mask = REDUCT_MODIFIEDFILECACHE;
							break;
						}

						case IDM_MODIFIEDLIST_CHK:
						{
							new_mask = REDUCT_MODIFIEDLIST;
							break;
						}

						case IDM_STANDBYLIST_CHK:
						{
							new_mask = REDUCT_STANDBYLIST;
							break;
						}

						case IDM_STANDBYLISTPRIORITY0_CHK:
						{
							new_mask = REDUCT_STANDBYPRIORITY0LIST;
							break;
						}

						case IDM_REGISTRYCACHE_CHK:
						{
							new_mask = REDUCT_REGISTRYCACHE;
							break;
						}

						case IDM_COMBINEMEMORYLISTS_CHK:
						{
							new_mask = REDUCT_COMBINEMEMORYLISTS;
							break;
						}

						default:
						{
							return FALSE;
						}
					}

					if ((ctrl_id == IDM_STANDBYLIST_CHK && !(mask & REDUCT_STANDBYLIST)) || (ctrl_id == IDM_MODIFIEDLIST_CHK && !(mask & REDUCT_MODIFIEDLIST)))
					{
						if (!_r_show_confirmmessage (hwnd, _r_locale_getstring (IDS_QUESTION_WARNING), NULL, L"IsShowWarningConfirmation", FALSE))
							return FALSE;
					}

					_r_config_setulong_ex (L"ReductMask2", (mask & new_mask) != 0 ? (mask & ~new_mask) : (mask | new_mask), NULL);

					break;
				}

				case IDM_CLEAN_WORKINGSET:
				case IDM_CLEAN_SYSTEMFILECACHE:
				case IDM_CLEAN_MODIFIEDFILECACHE:
				case IDM_CLEAN_MODIFIEDLIST:
				case IDM_CLEAN_STANDBYLIST:
				case IDM_CLEAN_STANDBYLISTPRIORITY0:
				case IDM_CLEAN_REGISTRYCACHE:
				case IDM_CLEAN_COMBINEMEMORYLISTS:
				{
					switch (ctrl_id)
					{
						case IDM_CLEAN_WORKINGSET:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_WORKINGSET);
							break;
						}

						case IDM_CLEAN_SYSTEMFILECACHE:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_SYSTEMFILECACHE);
							break;
						}

						case IDM_CLEAN_MODIFIEDFILECACHE:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_MODIFIEDFILECACHE);
							break;
						}

						case IDM_CLEAN_MODIFIEDLIST:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_MODIFIEDLIST);
							break;
						}

						case IDM_CLEAN_STANDBYLIST:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_STANDBYLIST);
							break;
						}

						case IDM_CLEAN_STANDBYLISTPRIORITY0:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_STANDBYPRIORITY0LIST);
							break;
						}

						case IDM_CLEAN_REGISTRYCACHE:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_REGISTRYCACHE);
							break;
						}

						case IDM_CLEAN_COMBINEMEMORYLISTS:
						{
							_app_memoryclean (hwnd, SOURCE_CMDLINE, REDUCT_COMBINEMEMORYLISTS);
							break;
						}

						default:
						{
							return FALSE;
						}
					}

					break;
				}

				case IDM_TRAY_DISABLE_1:
				{
					_app_config_invertboolean (L"AutoreductEnable", FALSE, NULL);
					break;
				}

				case IDM_TRAY_DISABLE_2:
				{
					_app_config_invertboolean (L"AutoreductIntervalEnable", FALSE, NULL);
					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				{
					_r_settings_createwindow (hwnd, &SettingsProc, 0);
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDOK: // process Enter key
				case IDC_CLEAN:
				case IDM_TRAY_CLEAN:
				{
					if (_r_sys_iselevated ())
					{
						_app_memoryclean (hwnd, SOURCE_MANUAL, 0);
					}
					else
					{
						if (_r_app_runasadmin ())
						{
							DestroyWindow (hwnd);
						}
						else
						{
							_r_show_message (hwnd, MB_OK | MB_ICONSTOP, _r_locale_getstring (IDS_STATUS_NOPRIVILEGES), NULL);
						}
					}

					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					_r_shell_opendefault (_r_app_getwebsite_url ());
					break;
				}

				case IDM_CHECKUPDATES:
				{
					_r_update_check (hwnd);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					_r_show_aboutmessage (hwnd);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

BOOLEAN NTAPI _app_parseargs (
	_In_ R_CMDLINE_INFO_CLASS type
)
{
	PR_STRING clean_args;
	ULONG mask = 0;

	switch (type)
	{
		case CmdlineClean:
		{
			_r_sys_getopt (_r_sys_getcommandline (), L"clean", &clean_args);

			if (clean_args)
			{
				if (_r_str_isequal2 (&clean_args->sr, L"full", TRUE))
					mask = REDUCT_MASK_ALL;

				_r_obj_dereference (clean_args);
			}

			if (mask == 0)
				mask = REDUCT_MASK_DEFAULT;

			_app_initialize (NULL);

			_app_memoryclean (NULL, SOURCE_CMDLINE, mask);

			return TRUE;
		}

		case CmdlineHelp:
		{
			_r_show_message (NULL, MB_OK | MB_ICONINFORMATION, NULL, L"Available options for memreduct.exe:\r\n\r\n-clean - clear default memory regions\r\n" "-clean:full - clear all memory regions");

			return TRUE;
		}
	}

	return FALSE;
}

typedef struct _APP_UPDATE_TARGET
{
	LPCWSTR install_dir;
	WCHAR path[MAX_PATH];
	HANDLE hprocess;
	HWND hwnd;
} APP_UPDATE_TARGET, *PAPP_UPDATE_TARGET;

BOOL CALLBACK _app_selfupdate_findwindow (
	_In_ HWND hwnd,
	_In_ LPARAM lparam
)
{
	PAPP_UPDATE_TARGET target = (PAPP_UPDATE_TARGET)lparam;
	WCHAR image_path[MAX_PATH];
	ULONG length = RTL_NUMBER_OF (image_path);
	ULONG pid = 0;
	HANDLE hprocess;
	LPWSTR file_name;

	// routine tags the main window of every Mem Reduct build with the app name
	if (!GetPropW (hwnd, APP_NAME) && !GetPropW (hwnd, APP_STORAGE_NAME))
		return TRUE;

	GetWindowThreadProcessId (hwnd, &pid);

	hprocess = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);

	if (!hprocess)
		return TRUE;

	// only the copy running from the install folder is replaced
	if (QueryFullProcessImageNameW (hprocess, 0, image_path, &length) && (file_name = wcsrchr (image_path, L'\\')) != NULL)
	{
		*file_name = UNICODE_NULL;

		if (_wcsicmp (image_path, target->install_dir) == 0)
		{
			_r_str_printf (target->path, RTL_NUMBER_OF (target->path), L"%s\\%s", image_path, file_name + 1);

			target->hprocess = hprocess;
			target->hwnd = hwnd;

			return FALSE;
		}
	}

	CloseHandle (hprocess);

	return TRUE;
}

// The update check downloads the release exe into the cache folder and runs it elevated as
// "<exe> /u /S /D=<install folder>" (routine's NSIS installer convention). Fork releases ship
// the plain exe, so it installs itself: close the copy running from the install folder, swap
// the files and start the new copy. Returns TRUE when this process was an update run.
BOOLEAN _app_selfupdate ()
{
	APP_UPDATE_TARGET target = {0};
	WCHAR self_path[MAX_PATH] = {0};
	WCHAR install_dir[MAX_PATH] = {0};
	WCHAR old_path[MAX_PATH + 8];
	LPCWSTR cmdline;
	LPCWSTR dir;
	LPWSTR *arga;
	ULONG_PTR length;
	INT numargs = 0;
	BOOLEAN is_update = FALSE;

	GetModuleFileNameW (NULL, self_path, RTL_NUMBER_OF (self_path));

	cmdline = GetCommandLineW ();
	arga = CommandLineToArgvW (cmdline, &numargs);

	if (arga)
	{
		for (INT i = 1; i < numargs; i++)
		{
			if (_wcsicmp (arga[i], L"/u") == 0)
				is_update = TRUE;
		}

		LocalFree (arga);
	}

	// "/D=" is the last argument and is never quoted, even when it contains spaces
	dir = wcsstr (cmdline, L" /D=");

	if (!is_update || !dir)
	{
		// leftover from a previous update; it can only be deleted once that copy has exited
		_r_str_printf (old_path, RTL_NUMBER_OF (old_path), L"%s.old", self_path);
		DeleteFileW (old_path);

		return FALSE;
	}

	_r_str_copy (install_dir, RTL_NUMBER_OF (install_dir), dir + 4);

	length = _r_str_getlength (install_dir);

	while (length && (install_dir[length - 1] == L' ' || install_dir[length - 1] == L'\\' || install_dir[length - 1] == L'"'))
		install_dir[--length] = UNICODE_NULL;

	if (!length)
		return TRUE;

	target.install_dir = install_dir;

	EnumWindows (&_app_selfupdate_findwindow, (LPARAM)&target);

	if (!target.path[0])
		_r_str_printf (target.path, RTL_NUMBER_OF (target.path), L"%s\\%s.exe", install_dir, APP_NAME_SHORT);

	if (_wcsicmp (self_path, target.path) == 0)
		return TRUE;

	// the running copy may be showing the update dialog; it exits through its own Exit command
	if (target.hprocess)
	{
		PostMessageW (target.hwnd, WM_COMMAND, MAKEWPARAM (IDM_TRAY_EXIT, 0), 0);

		if (WaitForSingleObject (target.hprocess, 15000) != WAIT_OBJECT_0)
		{
			TerminateProcess (target.hprocess, ERROR_SUCCESS);
			WaitForSingleObject (target.hprocess, 5000);
		}

		CloseHandle (target.hprocess);
	}

	_r_str_printf (old_path, RTL_NUMBER_OF (old_path), L"%s.old", target.path);

	DeleteFileW (old_path);

	// keep the installed copy until the new one is in place, so a failed copy can be undone
	if (GetFileAttributesW (target.path) != INVALID_FILE_ATTRIBUTES && !MoveFileExW (target.path, old_path, MOVEFILE_REPLACE_EXISTING))
	{
		MessageBoxW (NULL, L"Could not replace the installed copy. It was left unchanged.", APP_NAME, MB_OK | MB_ICONERROR);
	}
	else if (!CopyFileW (self_path, target.path, FALSE))
	{
		MoveFileExW (old_path, target.path, MOVEFILE_REPLACE_EXISTING);

		MessageBoxW (NULL, L"Could not install the update. The previous version was restored.", APP_NAME, MB_OK | MB_ICONERROR);
	}
	else if (!DeleteFileW (old_path))
	{
		MoveFileExW (old_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	}

	// the downloaded copy in the cache folder is no longer needed
	MoveFileExW (self_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);

	if ((INT_PTR)ShellExecuteW (NULL, NULL, target.path, NULL, install_dir, SW_SHOWNORMAL) <= 32)
		MessageBoxW (NULL, L"The update was installed, but the program could not be started.", APP_NAME, MB_OK | MB_ICONWARNING);

	return TRUE;
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	HWND hwnd;

	if (_app_selfupdate ())
		return ERROR_SUCCESS;

	if (!_r_app_initialize (&_app_parseargs))
		return ERROR_APP_INIT_FAILURE;

	hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCEW (IDD_MAIN), MAKEINTRESOURCEW (IDI_MAIN), &DlgProc);

	if (!hwnd)
		return ERROR_APP_INIT_FAILURE;

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCEW (IDA_MAIN));
}
