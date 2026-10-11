<h1 align="center">Mem Reduct</h1>

<p align="center">
	<a href="https://github.com/henrypp/memreduct/releases"><img src="https://img.shields.io/github/v/release/henrypp/memreduct?style=flat-square&include_prereleases&label=version" /></a>
	<a href="https://github.com/henrypp/memreduct/releases"><img src="https://img.shields.io/github/downloads/henrypp/memreduct/total.svg?style=flat-square" /></a>
	<a href="https://github.com/henrypp/memreduct/issues"><img src="https://img.shields.io/github/issues-raw/henrypp/memreduct.svg?style=flat-square&label=issues" /></a>
	<a href="https://github.com/henrypp/memreduct/graphs/contributors"><img src="https://img.shields.io/github/contributors/henrypp/memreduct?style=flat-square" /></a>
	<a href="https://github.com/henrypp/memreduct/blob/master/LICENSE"><img src="https://img.shields.io/github/license/henrypp/memreduct?style=flat-square" /></a>
</p>

-------

<p align="center">
	<img src="/images/memreduct.png?cachefix" />
</p>

### Description:
Lightweight real-time memory management application to monitor
and clean system memory on your computer.

The program used undocumented internal system features (Native API) to clear system cache (system working set, working set, standby page lists, modified page lists) with variable result ~10-50%. Application it is compatible with <s>Windows XP SP3</s> Windows 7 SP1 and higher operating systems.

You can download either the installer or portable version. For correct working you are require administrator rights.

```
To activate portable mode, create "memreduct.ini" in application folder, or move it from "%APPDATA%\Henry++\Mem Reduct".
```

### Fork additions (Windows 11 24H2/25H2):
<p align="center">
	<img src="/images/fork-main-window.png" alt="Main window with the new Commit charge, Memory lists and Pagefiles groups" />
	<img src="/images/fork-pagefile-report.png" alt="File → Pagefile report" />
</p>

- Cleaning runs on a worker thread, so the window stays responsive (#312). Measured on 25H2 26200: 3.5.2 blocked its UI for the whole clean (299 ms here), the fork for at most 7 ms.
- "Commit charge" (Task Manager "Committed"), "Memory lists" (standby/modified/free) and "Pagefiles" (one row per pagefile, used / size) groups. Percentages no longer show NaN when no pagefile exists (#262, #263).
- **File → Pagefile report**: per-pagefile usage, peak and configured size, commit charge, modified pages bound for the pagefile, and processes ranked by private commit not in RAM (pagefile, compressed, or never touched). Each process amount is an upper bound on what it could have in the pagefile, not a measurement.
- **Settings → Do not flush modified pages when pagefile is N% full** (default on, 90%): flushing the modified list writes into the pagefile, so it is skipped when the pagefile is nearly full.
- **Settings → Wipe pagefile at shutdown**: toggles `ClearPageFileAtShutdown` (slower shutdown; zeroes the file each shutdown).
- **Settings → Smart standby purge** (ISLC-style): purges the standby list only when standby > X MB and free < Y MB, at most every 30 s, silently.
- "Modified file cache" flushes fixed local volumes only (no removable, network or offline volumes).
- The automatic update check at startup is disabled so an upstream release cannot overwrite the fork; *Help → Check for updates* still works manually.
- Fixes for bugs that also exist upstream: rows now turn the warning/danger colour; the "Clean when above" and "Clean every" tray submenus are sorted by value; intervals of 1–4 minutes show up and are ticked in the tray menu.

About "cleaning the pagefile": measured on Windows 11 25H2, only the owning process can release its pagefile space by freeing or modifying (writing to) that memory, or exiting. The report's process ranking includes private commit that was never touched, so each amount is an upper bound on what that process could have in the pagefile, not a measurement of pagefile contents or an amount restarting it would necessarily release. Reading pages back into RAM (prefetching, `ReadProcessMemory`, or reading them) does not release pagefile space, and no supported API releases it for another process. Writing into other processes' memory would risk corrupting data and trip anti-cheat/EDR, so the fork displays actual per-pagefile usage instead of rewriting foreign memory. Unused data sitting in the pagefile does not slow the system down; what does is heavy *paging activity*, which happens when commit is high and RAM is short.

`memreduct.ini` keys (section `[memreduct]`):

| Key | Default | Meaning |
|---|---|---|
| `PagefileGuardEnable` | `true` | Enable the pagefile guard |
| `PagefileGuardPercent` | `90` | Pagefile usage (%) at which the modified page list is not flushed |
| `SmartStandbyEnable` | `false` | Enable smart standby purge |
| `SmartStandbyListMB` | `1024` | Purge when the standby list is at least this large... |
| `SmartStandbyFreeMB` | `1024` | ...and free memory is below this |
| `WorkingSetExclusions` | (empty) | `game.exe; obs64.exe` - processes skipped by working set trimming |

Build without Visual Studio: `build_portable.bat [path\to\portable-msvc]` (toolchain from [portable-msvc.py](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977)), with the [routine fork](https://github.com/Hantu-Raya/routine/tree/fork/win11-pagefile) (branch `fork/win11-pagefile`) checked out as `..\routine` next to this folder.

### System requirements:
- Windows 7, 8, 8.1, 10, 11 (64-bit/ARM64)
- An SSE2-capable CPU
- <s>KB2533623</s> [KB3063858](https://www.microsoft.com/en-us/download/details.aspx?id=47442) update for Windows 7 was required

### Donate:
- [Bitcoin](https://www.blockchain.com/btc/address/1LrRTXPsvHcQWCNZotA9RcwjsGcRghG96c) (BTC)
- [Ethereum](https://www.blockchain.com/explorer/addresses/eth/0xe2C84A62eb2a4EF154b19bec0c1c106734B95960) (ETH)
- [Yandex Money](https://yoomoney.ru/to/4100115776040583) (RUB)
- [Paypal](https://paypal.me/henrypp) (USD)

### GPG Signature:
Binaries have GPG signature `memreduct.exe.sig` in application folder.

- Public key: [pubkey.asc](https://raw.githubusercontent.com/henrypp/builder/master/pubkey.asc) ([pgpkeys.eu](https://pgpkeys.eu/pks/lookup?op=index&fingerprint=on&search=0x5635B5FD))
- Key ID: 0x5635B5FD
- Fingerprint: D985 2361 1524 AB29 BE73 30AC 2881 20A7 5635 B5FD
---
- Website: [github.com/henrypp](https://github.com/henrypp)
- Support: sforce5@mail.ru
---
(c) 2011-2026 Henry++
