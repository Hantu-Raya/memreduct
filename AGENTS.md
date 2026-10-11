# Repository Guidelines

## Project Overview

Mem Reduct++ is an unofficial Windows 11 (24H2/25H2) fork of [Mem Reduct](https://github.com/henrypp/memreduct) by Henry++. It is a Win32 tray app in C that monitors memory and cleans it through NT native APIs: working sets, system file cache, standby/modified lists, registry cache, and memory combining. The fork adds:

- a worker-thread clean, so the UI no longer freezes;
- commit charge, memory-list and per-pagefile rows;
- File → Pagefile report;
- a pagefile guard, "wipe pagefile at shutdown", ISLC-style smart standby purge, and working-set exclusions;
- updates from this fork, with a self-installing release exe.

Henry++ owns the original work. Keep the attribution, copyright (`APP_AUTHOR`, `APP_COPYRIGHT`) and the Henry++ donation links prominent. Fork problems go to `Hantu-Raya/memreduct` issues, never upstream.

## Architecture & Data Flow

```text
wWinMain (src/main.c)
  -> _app_selfupdate()       "/u /S /D=<dir>": install this exe over <dir>, relaunch, exit
  -> _r_app_initialize()     routine: mutex "memreduct", config, -clean/-help args (_app_parseargs)
  -> _r_app_createwindow()   IDD_MAIN dialog, DlgProc; routine wraps it (_r_app_maindlgproc)
  -> _r_wnd_message_callback()

DlgProc
  WM_INITDIALOG   privileges, SetTimer(UID, 1000 ms) -> _app_timercallback
  timer           _app_getmemoryinfo/_ex -> auto clean / smart standby -> tray icon -> listview rows
  WM_COMMAND      menus, Clean button, tray menu (via _r_menu_popup(..., TRUE)), IDM_CLEAN_* per region
  RM_TRAYICON     WM_APP+11; click actions TrayActionDc/TrayActionMc; WM_CONTEXTMENU builds the tray menu
  RM_CLEANDONE    WM_APP+0x100; worker hands its context back to the UI thread

Cleaning:
  _app_memoryclean (UI)  -> APP_CLEAN_CONTEXT snapshot (mask, flags, exclusions, guard %)
                         -> CreateThread(_app_memoryclean_thread) -> _app_memoryclean_perform (native calls only)
                         -> PostMessage(RM_CLEANDONE) -> _app_memoryclean_finish (UI: stats, popup, log, free)
```

- `config.is_cleaning` is a `volatile LONG`. `InterlockedCompareExchange` is the real claim; a plain read is only an early-out. With no HWND (command-line clean) or a failed `CreateThread`, the clean runs perform + finish inline.
- The worker must not touch UI or config. Settings are captured into the context first. The only exception is the guard note's `_r_locale_getstring`.
- `StatisticLastReduct` (ini) drives interval/usage auto-clean. Non-smart cleans write it at start and finish. `SOURCE_SMART` never writes it.
- `IDM_CLEAN_*` dropdown items use `SOURCE_CMDLINE` on purpose: no confirmation, and command-line style result reporting.
- Updates (routine `rapp.c`):
  1. `_r_update_check` fetches `APP_UPDATE_URL`, which is this repo's `VERSION`.
  2. It compares versions with `_r_str_versioncompare` (4 parts, so `3.5.3.1 < 3.5.3.2`).
  3. It downloads the app entry to `<cache>\update-memreduct-*.exe`, checks it (below), and runs it elevated as `/u /S /H=<sha256> /D=<install dir>` (`/H=` since 3.5.3.2; `/D=` stays last).
  4. `_app_selfupdate` then:
     - takes the last ` /D=` and requires an existing absolute folder short enough for `<dir>\memreduct.exe.old`; anything else is a no-op that still exits;
     - posts `IDM_TRAY_EXIT` to the copy whose image is in that dir (window prop = `APP_NAME` or `APP_STORAGE_NAME`), terminating it if it is still running after 15 s;
     - renames the installed exe to `.old`, copies itself in, and rolls back on failure; if the rollback fails too it keeps `.old`, says where, and doesn't relaunch;
     - relaunches through the desktop shell (`IShellDispatch2::ShellExecute`), so the app runs as the signed-in user and re-elevates through its skip-UAC task. It falls back to a direct `ShellExecuteW`.
  - Activating a running instance also matches the `APP_STORAGE_NAME` title/prop (routine `_r_app_initialize`), so starting Mem Reduct++ while the original Mem Reduct runs shows that window.
  - Update authentication (routine, enabled by `APP_UPDATE_PUBKEY` in `app.h`, raw P-256 X||Y):
    - `VERSION` must end with `signature=<r||s hex>`, an ECDSA P-256/SHA-256 signature over the ASCII text before it (the CR of each CRLF removed). `_r_update_verifymanifest` rejects anything else: no signature, a lone CR, non-ASCII, text after the signature, or more than 64 KiB. Unsigned manifests (all before 3.5.3.2) are ignored.
    - Each record is `name=version|url|sha256`. Installer records without a hash are refused.
    - `_r_update_verifyfile` hashes the download through a handle that denies write/delete/rename. `_r_update_install` holds that handle until the elevated process starts and passes the hash as `/H=`. A mismatch deletes the file and shows `STATUS_INVALID_IMAGE_HASH`.
    - `_app_selfupdate` with `/H=` pins its own file the same way and re-checks it before closing the old copy, keeping the pin through the copy. Without `/H=` (callers before 3.5.3.2) it uses the old path; keep that working.
    - `_r_crypt_getfilehash` returns a hash only after a clean end of file.
    - Sign with `python sign_version.py`; its `verify` applies the app's exact rules. The private key lives outside the repo (`E:\ram\keys\`). Never commit `*.pem`.
    - Test harness: `E:\ram\notes\sigtest\` (local server; cases in `make_manifest.py`); results in `E:\ram\notes\test3.md`.
    - The exe itself is not code-signed (SmartScreen may warn). Don't claim otherwise.
  - Update dialog pages (`_r_update_navigate`) live in `R_UPDATE_INFO.page_config`/`page_content` (two alternating slots), never on the stack. The task dialog re-reads a page's `TASKDIALOGCONFIG` after `TDM_NAVIGATE_PAGE` returns. Pages sent from the check/download threads crashed 3.5.3.1 once those threads had exited (dump: UI thread reading `dwFlags` from a freed stack). Copy any caller text into the slot.

## Key Directories

- `src/`: the app. `main.c` holds everything (UI, timer, cleaning, memory queries, report, self-update). `main.h` holds masks, row/group indices and contexts. `app.h` holds the identity and fork overrides. `compat.h` holds SDK/routine gaps (mount manager, memory-list structs). Also `resource.h`/`resource.rc` and `res/manifest.xml`.
- `../routine/src/`: the helper library, compiled into the exe. Its `fork/win11-pagefile` branch is required. It provides `routine.c/.h` (strings, objects, memory, NT wrappers, listview/menu helpers), `rapp.c/.h` (app lifecycle, config, locale, autorun, updater), `rconfig.h` (feature switches, defaults) and `ntapi.h`.
- `bin/i18n/*.ini` + `bin/memreduct.lng`: localization (UTF-16LE, CRLF). `.lng` is generated by Henry++'s `../builder`, which is absent here, so don't hand-edit it. New strings fall back to the English in `resource.rc`.
- `images/`: README screenshots.
- `E:/ram/notes/` (outside the repo, unversioned): probe scripts, deploy and E2E scripts, audit reports.

## Development Commands

```powershell
# build (portable MSVC 14.51 + SDK 10.0.28000 in ..\tools\msvc; no Visual Studio needed)
powershell -NoProfile -c "cmd /c .\build_portable.bat"        # -> bin\64\memreduct.exe (+ .pdb)

# deploy to the installed copy (stops it, copies, hash-checks, restarts)
powershell -NoProfile -ExecutionPolicy Bypass -File E:\ram\notes\deploy.ps1

# run
bin\64\memreduct.exe            # GUI/tray
bin\64\memreduct.exe -clean     # headless clean of configured regions; -clean:full for all
```

- Stop any `memreduct.exe` running from `bin\64` before building, or linking fails with `LNK1104`.
- `[WARNING!] Undefined: …` / `Config already defined: …` lines are `#pragma message` output and are expected. Real warnings fail the build (`/W3 /WX`).
- `build.bat`, `build_locale.bat` and `build_vc.bat` need `../builder` or Visual Studio 2026 and don't work in this setup.
- There is no linter or formatter. Match the surrounding style by hand.

## Code Conventions & Common Patterns

- C (`/std:clatest`), not C++. Tabs, a space before `(` in calls and control statements, braces on their own lines, SAL-annotated multi-line parameter lists.
- Names: `_app_*` for app functions, `_r_*` for routine. `APP_*`/`PAPP_*` for app structs, `R_*`/`PR_*` for routine types. `IDM_*` menu, `IDC_*` controls, `IDS_*` strings, `RM_*` private messages.
- Errors: `NTSTATUS` + `NT_SUCCESS`. A failed cleaning call logs `_r_log (LOG_LEVEL_ERROR, NULL, L"NtSetSystemInformation", status, L"<op>")` and continues. Query helpers free their buffers and return status or NULL, and show no UI.
- Memory and strings:
  - `_r_mem_allocate` zero-fills and raises on failure, so it never returns NULL. Free with `_r_mem_free`.
  - `PR_STRING` from `_r_config_getstring_ex` and similar is reference-counted; release it with `_r_obj_dereference`.
  - `_r_locale_getstring` returns a borrowed `LPWSTR`; never free it.
  - `_r_str_*` capacities are in WCHARs (`RTL_NUMBER_OF`), while `R_STRING.length` and `UNICODE_STRING.Length` are in bytes.
- Config: `_r_config_get*_ex (key, default, NULL)` / `_r_config_set*_ex (key, value, NULL)`, in section `[memreduct]`. Setters write the ini immediately. New ini keys belong in the README table.
- UI text: add an `IDS_*` to `resource.h` and an English string to `resource.rc`, then call `_r_locale_getstring (IDS_*)`.
- NT definitions missing from the SDK or routine go in `src/compat.h`.
- Routine APIs that behave differently from what the name suggests (each has caused a real bug here):
  - `_r_menu_popup (menu, hwnd, pt, is_sendmessage)` uses `TPM_RETURNCMD`. It only sends `WM_COMMAND` when the last argument is `TRUE`.
  - `_r_listview_setitem` applies image, group and lParam only for subitem 0. `I_DEFAULT` (-8) means unchanged; lParam 0 is a real value. Row colours read the subitem-0 lParam (see `_app_setrow`).
  - `_r_update_navigate (info, buttons, flags, icon, title, content, …)`: the icon comes before the title. Swapping them makes `TaskDialogIndirect` fail with 0x80070715 and the dialog closes itself.
  - `_r_show_confirmmessage` returns TRUE without asking once its config key is FALSE.
- Identity macros in `src/app.h`:
  - `APP_NAME` "Mem Reduct++" is display only.
  - `APP_STORAGE_NAME` "Mem Reduct" names the `%APPDATA%\Henry++\Mem Reduct` folder and the HKCU Run value. Don't change it, or users lose their settings and autorun.
  - `APP_NAME_SHORT` "memreduct" names the mutex, the ini section, the skip-UAC task and the `VERSION` key.
  - `APP_WEBSITE_URL`, `APP_UPDATE_URL` and `APP_VERSION_TYPE` are honoured by routine's `rapp.h`.
  - `APP_COMMENT` and `APP_STORAGE_NAME` `#undef` routine's defaults because `resource.rc` includes `rconfig.h` before `app.h`.

## Important Files

| File | Why it matters |
|---|---|
| `src/main.c` | `wWinMain`, `_app_selfupdate`, `DlgProc`, `_app_timercallback`, `_app_memoryclean*`, `_app_getmemoryinfo_ex`, `_app_showpagefilereport` |
| `src/app.h` | Version (`APP_VERSION` + `APP_VERSION_RES`), names, fork URLs |
| `VERSION` | Update manifest read by every installed copy: `memreduct=<ver>\|<release exe url>;` then `language=…;`. LF separators, no trailing newline. Pushing it to `fork/win11-pagefile` publishes an update immediately. |
| `build_portable.bat` | The working build. Defines `APP_HAVE_AUTORUN/SKIPUAC/TRAY/UPDATES` (keep `memreduct.vcxproj` in step) |
| `CHANGELOG.md` | CRLF, `\-` bullets with blank lines between them; newest entry first |
| `README.md` | Attribution header, fork-additions list, ini key table, download/update docs |
| `../routine/src/rapp.c` | Config paths, autorun, updater (`_r_update_*`) |

## Runtime/Tooling Preferences

- Windows 11 x64. Cleaning needs admin; the manifest is `asInvoker`, and elevation comes via UAC or the skip-UAC task. Fork builds are x64 only.
- Toolchain: the portable MSVC at `E:\ram\tools\msvc` (via `portable-msvc.py`). Don't install Visual Studio or anything else globally.
- Probe scripts use Python 3 (stdlib `ctypes` only) and Windows PowerShell 5. Run them from an elevated shell when the app is elevated.
- Git:
  - `fork` = `github.com/Hantu-Raya/{memreduct,routine}`; the default branch is `fork/win11-pagefile`.
  - `origin` = Henry++ upstream. Never push there.
  - Push, tag and release only when the user asks.
- Consult the second model (gpt-6.1-sol, medium) with `omp -p --no-session --model openai-codex/gpt-6.1-sol --thinking medium --tools=read,grep,glob,find` from `E:\ram\notes`. Without `--no-session` it resumes the caller's session.

## Testing & QA

There is no unit-test framework and no CI. Evidence comes from the `/WX` build plus driving the real app with the probes in `E:\ram\notes`. Command IDs: 103 Clean, 162 check updates, 163 About, 164 show/hide, 165 tray Clean, 180–187 per-region clean, 188 pagefile report.

| Change | Check |
|---|---|
| Tray/menu dispatch | `python tray_menu_probe.py` then `python tray_menu_select.py 165`; `StatisticLastReduct` in the ini must change. Control: `python mr_ctl.py 103`. Posting 165 directly bypasses the popup and proves nothing. |
| Menu contents/order | `python tray_menu_dump.py` |
| Cleaning/threading | `python clean_probe.py 180 8` (standby/modified/working-set deltas); `python ui_latency.py 25` (worst UI stall; fork 7 ms vs 3.5.2 299 ms) |
| Smart standby / guard | Temporarily set ini thresholds so they trigger. Back up the ini first and restore it byte-for-byte. Smart purge must not change `StatisticLastReduct`. Guard: `clean_probe.py 183` skipped vs flushed. |
| Update dialog | `python update_probe.py` (the dialog must stay open) |
| Self-update | `powershell -File selfupdate_test.ps1 -NewExe <exe>` runs the exact `/u /S /D=` path and checks 7 points: old exited, hash, single instance, path, no `.old`, ini, autorun |
| Row colours, report, About | Screenshot the real window. Colour checks are visual only. |

- Changing the user's ini, autorun or installed exe for a test needs a backup and a restore.
- Remove temporary instrumentation before committing.
- Tested only on Windows 11 Enterprise 25H2 26200 with two pagefiles. Windows 10, many-pagefile machines and real taskbar mouse clicks are untested.

## Durable decisions

- Never write into other processes' memory to "reclaim" pagefile space. Measured: only the owner freeing, writing or exiting releases slots, and remote page-state changes fail. Foreign writes race pending I/O and trip anti-cheat/EDR (`E:\ram\notes\codex_r1.md`, `codex_r2.md`).
- The pagefile report's per-process figure (private commit − private working set) is an upper bound, not pagefile contents.
- Releases:
  1. Bump `APP_VERSION`/`APP_VERSION_RES` (`3.5.3.N`).
  2. Add a CHANGELOG entry.
  3. Build and test.
  4. Tag `v3.5.3.N`.
  5. Create a GitHub release with the asset `memreduct-plusplus-3.5.3.N-x64.exe` plus SHA-256. Download it back and verify the hash.
  6. Only then run `sign_version.py release` against the downloaded asset, verify, and push `VERSION`. Every installed copy acts on it.
