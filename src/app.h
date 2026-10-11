// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#pragma once

#define APP_NAME L"Mem Reduct++"
#define APP_NAME_SHORT L"memreduct"
#define APP_VERSION L"3.5.3.2"
#define APP_VERSION_RES 3,5,3,2
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

// Updates are accepted only when VERSION is signed by the matching private key (ECDSA P-256,
// public X || Y below) and the downloaded exe matches the SHA-256 listed in it. Sign with
// sign_version.py; the private key never goes into this repository.
#define APP_UPDATE_PUBKEY { \
	0xA3, 0x2A, 0x78, 0xE6, 0xA3, 0x9E, 0x45, 0x39, 0x57, 0x6D, 0x7C, 0xF9, 0xE3, 0x19, 0x07, 0xB6, \
	0xF1, 0x56, 0x62, 0x61, 0x44, 0x61, 0xA5, 0xDC, 0x27, 0xF3, 0x32, 0xA7, 0x7E, 0x61, 0x00, 0x8F, \
	0x4D, 0x5B, 0x55, 0xB3, 0xE4, 0x2D, 0x35, 0x72, 0xFC, 0xAF, 0xC8, 0x7C, 0x4F, 0x15, 0x1B, 0xC6, \
	0x88, 0x2E, 0x81, 0x23, 0x3F, 0x88, 0x47, 0x4E, 0x3A, 0x97, 0x8B, 0xFF, 0xB6, 0x42, 0x3C, 0xB7 \
}
