// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#pragma once

#define APP_NAME L"Mem Reduct++"
#define APP_NAME_SHORT L"memreduct"
#define APP_VERSION L"3.5.3.1"
#define APP_VERSION_RES 3,5,3,1
#define APP_AUTHOR L"Henry++"
#define APP_COPYRIGHT L"(c) 2011-2026 " APP_AUTHOR L". All Rights Reversed."

// keep the original settings folder (%APPDATA%\Henry++\Mem Reduct) and autorun entry, so this
// build is a drop-in replacement for an installed Mem Reduct
#undef APP_STORAGE_NAME // resource.rc includes rconfig.h (which sets a default) before this file
#define APP_STORAGE_NAME L"Mem Reduct"

// Unofficial fork build (Mem Reduct++). Author and copyright stay Henry++'s (the author also
// locates the settings folder); links and update checks go to the fork so fork problems are
// not reported to the original author.
#define APP_FORK_URL L"https://github.com/Hantu-Raya/memreduct"
#define APP_WEBSITE_URL APP_FORK_URL
#define APP_UPDATE_URL L"https://raw.githubusercontent.com/Hantu-Raya/memreduct/refs/heads/fork/win11-pagefile/VERSION"
#define APP_VERSION_TYPE L"(unofficial Windows 11 fork)"
#undef APP_COMMENT // resource.rc includes rconfig.h (which sets a default) before this file
#define APP_COMMENT "Mem Reduct++: unofficial Windows 11 fork of Mem Reduct by Henry++ - https://github.com/Hantu-Raya/memreduct"
