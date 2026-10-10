// Mem Reduct (fork)
// Definitions missing from the published "routine" library and the user-mode SDK.

#pragma once

//
// Mount manager (mountmgr.h is WDK-only)
//

#ifndef MOUNTMGR_DEVICE_NAME
#define MOUNTMGR_DEVICE_NAME L"\\Device\\MountPointManager"
#endif // MOUNTMGR_DEVICE_NAME

#ifndef IOCTL_MOUNTMGR_QUERY_POINTS
#define IOCTL_MOUNTMGR_QUERY_POINTS CTL_CODE(0x0000006D, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif // IOCTL_MOUNTMGR_QUERY_POINTS

typedef struct _MOUNTMGR_MOUNT_POINT
{
	ULONG SymbolicLinkNameOffset;
	USHORT SymbolicLinkNameLength;
	USHORT Reserved1;
	ULONG UniqueIdOffset;
	USHORT UniqueIdLength;
	USHORT Reserved2;
	ULONG DeviceNameOffset;
	USHORT DeviceNameLength;
	USHORT Reserved3;
} MOUNTMGR_MOUNT_POINT, *PMOUNTMGR_MOUNT_POINT;

typedef struct _MOUNTMGR_MOUNT_POINTS
{
	ULONG Size;
	ULONG NumberOfMountPoints;
	MOUNTMGR_MOUNT_POINT MountPoints[1];
} MOUNTMGR_MOUNT_POINTS, *PMOUNTMGR_MOUNT_POINTS;

// \??\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
#define MOUNTMGR_IS_VOLUME_NAME(s) ( \
	((s)->Length == 96 || ((s)->Length == 98 && (s)->Buffer[48] == L'\\')) && \
	(s)->Buffer[0] == L'\\' && ((s)->Buffer[1] == L'?' || (s)->Buffer[1] == L'\\') && \
	(s)->Buffer[2] == L'?' && (s)->Buffer[3] == L'\\' && \
	(s)->Buffer[4] == L'V' && (s)->Buffer[5] == L'o' && (s)->Buffer[6] == L'l' && \
	(s)->Buffer[7] == L'u' && (s)->Buffer[8] == L'm' && (s)->Buffer[9] == L'e' && \
	(s)->Buffer[10] == L'{' && (s)->Buffer[19] == L'-' && (s)->Buffer[24] == L'-' && \
	(s)->Buffer[29] == L'-' && (s)->Buffer[34] == L'-' && (s)->Buffer[47] == L'}')

typedef PMOUNTMGR_MOUNT_POINTS *PMOUNTMGR_MOUNT_POINTS_PTR;

//
// Memory list information (SystemMemoryListInformation query)
//

typedef struct _APP_SYSTEM_MEMORY_LIST_INFORMATION
{
	SIZE_T ZeroPageCount;
	SIZE_T FreePageCount;
	SIZE_T ModifiedPageCount;
	SIZE_T ModifiedNoWritePageCount;
	SIZE_T BadPageCount;
	SIZE_T PageCountByPriority[8];
	SIZE_T RepurposedPagesByPriority[8];
	SIZE_T ModifiedPageCountPageFile;
} APP_SYSTEM_MEMORY_LIST_INFORMATION, *PAPP_SYSTEM_MEMORY_LIST_INFORMATION;

//
// Volume device characteristics (wdm.h / winioctl.h)
//

#ifndef FILE_REMOVABLE_MEDIA
#define FILE_REMOVABLE_MEDIA 0x00000001
#endif // FILE_REMOVABLE_MEDIA

#ifndef FILE_READ_ONLY_DEVICE
#define FILE_READ_ONLY_DEVICE 0x00000002
#endif // FILE_READ_ONLY_DEVICE

#ifndef FILE_FLOPPY_DISKETTE
#define FILE_FLOPPY_DISKETTE 0x00000004
#endif // FILE_FLOPPY_DISKETTE

#ifndef FILE_REMOTE_DEVICE
#define FILE_REMOTE_DEVICE 0x00000010
#endif // FILE_REMOTE_DEVICE

#ifndef FILE_DEVICE_DISK
#define FILE_DEVICE_DISK 0x00000007
#endif // FILE_DEVICE_DISK

#ifndef FILE_DEVICE_VIRTUAL_DISK
#define FILE_DEVICE_VIRTUAL_DISK 0x00000024
#endif // FILE_DEVICE_VIRTUAL_DISK
