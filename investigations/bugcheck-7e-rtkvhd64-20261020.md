# Track B — BugCheck 0x7E at RTKVHD64.sys+0x6A7E (DESKTOP-RSUO2LV, 2026-10-20)

Separate incident from Track A (#328 I226 unload hang, `issue-328-unload-hang-20261020.md`). Causal relationship A↔B: **unproven**.

## FACTS

| | Crash 1 | Crash 2 |
|---|---|---|
| BugCheck | 0x7E, `C0000005` | 0x7E, `C0000005` |
| IP | `FFFFF802'69DC6A7E` | `FFFFF802'2C806A7E` |
| RTKVHD64.sys base / offset | `FFFFF802'69DC0000` / **+0x6A7E** | `FFFFF802'2C800000` / **+0x6A7E** |
| Access | **write to 0x0** | **write to 0x0** |
| exr / cxr | `FFFF8B8A8C53F2C8` / `FFFF8B8A8C53EAE0` | `FFFFBC07A895F2C8` / `FFFFBC07A895EAE0` |
| Minidump (SHA256) | `102026-6843-01.dmp` `407AC11E…32A3` | `102026-6937-01.dmp` `975278BE…DF88` |
| Crash window | after 11:40:08.97 (last DbgView line), boot 11:40:46 | after 13:33:07, boot 13:35:23 |
| kd.exe started on host | 11:39:37 (on 1.3 GB NDIS dump copy) | 13:33:07 (on 2.7 MB minidump copy) |
| IntelAvbFilter.sys in module list | present (unload hang in progress) | **absent** |

- Faulting module identification by Q, off-host, from both minidumps: RTKVHD64.sys, same image size and PE timestamp in both.
- Driver: Realtek Audio, codec ALC255 `HDAUDIO\FUNC_01&VEN_10EC&DEV_0255&SUBSYS_1028085A`; package `oem22.inf` = `hdxdellbc2018.inf_amd64_73fedd8b047069f7` (Dell OEM); **6.0.8710.1, 2019-05-22**; file 6,866,216 B, SHA256 prefix `D0101F7F50219774`; service `IntcAzAudAddService` (Running, Manual). Host OS: Windows 11 build 26200.
- No 0x7E in the previous 60 days (only 5× 0x9F param1=4 on 2026-10-08; owning driver not examined).
- System and Application logs: no events in the ~2 min before either crash (buffered events may be lost on bugcheck). Defender/Operational: no events in either window.
- Both kd runs produced no analysis output before the crash.

## THEORIES (unverified)

| # | Theory | For | Against / missing |
|---|---|---|---|
| B1 | kd.exe/dbgeng activity causes an audio event (e.g. a beep / sound) that reaches RTKVHD64 in a state where a pointer is NULL | 2/2 kd runs → crash within ~1–2 min; faulting module is the audio driver | Mechanism not shown; requires the crashing stack (portcls/ks/beep frames?) |
| B2 | Audio device power transition (idle → active) bug in the 2019 driver, triggered by any audio activity after long idle | old driver on new OS; 0x9F history | 0 crashes in 60 days outside kd runs |
| B3 | Coincidence | — | 2/2 with identical offset makes coincidence unlikely |

## Relationship to Track A

- B did not cause A: A (unload hang) began 09:26:34, B occurred 11:40 and 13:35.
- A is not required for B: crash 2 occurred with IntelAvbFilter not loaded.
- Crash 1 **terminated** A's live state (I226 hang destroyed before E2 could be captured). Whether A's state made crash 1 more likely: unknown.

## Remediation (proposed, NOT performed)

1. Never run kd/WinDbg on this host (in force).
2. Off-host: analyze both minidumps with `HIL-evidence\offhost-kit\trackB_rtkvhd_0x7e.txt`; capture the full stack (callers of RTKVHD64+0x6A7E) to confirm/refute B1.
3. Host change options (require Q approval, outside evidence collection): update Realtek ALC255 driver from Dell for SUBSYS 1028085A, or switch to Microsoft HD Audio class driver, or disable the audio device on this HIL host if audio is not needed.
