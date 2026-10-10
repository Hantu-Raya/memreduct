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
