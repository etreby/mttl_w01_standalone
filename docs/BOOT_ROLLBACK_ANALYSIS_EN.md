# MTTL-W01: boot confirmation with automatic rollback, keeping the OEM loader

Read-only analysis, 2026-11-22. No repo files were modified and no device was contacted.

Every claim is tagged with where it comes from:

- **[EV-repo]**: evidence from this checkout (the repository root), cited per file as `file:line`. The working tree was being edited concurrently, so line numbers may drift.
- **[EV-sdk]**: evidence from the extracted Ameba-1 SDK (the SDK extracted by `builder/linux_build.py`).
- **[EV-loader]**: evidence from disassembling a binary that is **byte-identical** to the OEM loader (see §0).
- **[EXT]**: external documentation or decompilation.
- **[INF]**: inference that has not been verified.

## Verdict

**Feasible without changing the loader, with caveats.**

The loader keeps no boot counter, does no checksum, and has no fallback once it has chosen a slot. It does, however, choose purely from the two slot signatures, with a fixed and asymmetric tie-break. Three facts make rollback possible:

- Changing NEW to OLD, or invalidating a slot, is a monotonic bit-clear.
- Image2 executes from SRAM, so a running image can safely modify its own slot's flash.
- A slot whose signature is not valid is simply skipped by the loader, never "half-selected".

The design is a **one-shot trial boot**:

1. The updated image's first flash write *arms* rollback: it changes a signature so that the next boot selects the old image.
2. Only after a health window does it write the *confirming* change.

Any hang, crash, reset or power loss between those two writes boots the old image.

Caveats:

1. **The two directions need different writes.** Slot B, the system-data OTA slot at 0x80000, wins ties.
2. **One direction needs a 4 KiB header-sector rewrite at confirm time.** In the recommended variant (§4) this happens only in slot B. Every torn state there falls back to slot A, so the design adds no new brick path.
3. **A hang before the arm write is not rolled back.** The device stays stuck until power is cycled, and then the trial boot repeats.
4. **The protocol only protects updates to a trial-capable image that are committed by a trial-capable image.** The first trial-capable build, installed by the current 2.1.x firmware, has no protection. Stock and older images must keep the existing commit.
5. Several behaviours must be confirmed on hardware (§7).

---

## 0. Provenance: the OEM loader is a known public binary

**The fingerprint.** The repo fingerprints the loader as 14984 bytes with SHA-256 `4a3d3f25…f428`. The hash covers `dump[32:32+14984]`, and the size is read from the word at flash 0x10. [EV-repo]

- `source/boot_identity.json`
- `recovery/core.py:8-9` and `:37`
- `source/standalone/firmware/sdk_ota_runtime.c:37-49` (`boot_compatible`)

**The match.** `github.com/ambiot/amb1_arduino` (branch `dev`), file `Arduino_package/ameba_tools_linux/bsp/image/ram_1.r.bin`, last changed in commit `1619ba437492df1c8d167a8a606a14f6375998c6` (2020-01-08). It is 14984 bytes with SHA-256 `4a3d3f255f94aae0efceb75c3cae54ae8c7339f75f80ab2fe80be3ab66b1f428`. **That is an exact match to the device fingerprint.** `history_folders/ameba_tools/` contains the same file.

Temporary copies (the scratchpad is not durable):

- `(analysis workspace, not committed) oem_loader_match.bin`
- `(analysis workspace, not committed) oem.dis`, a Thumb disassembly at VMA 0x10000BC8

Re-create the disassembly with `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x10000bc8`. The load address comes from the 32-byte `ram_1.p.bin` header.

**Non-matching candidates:**

| Candidate | Size (bytes) |
|---|---|
| pvvx SDK v3.5a `ram_1.r.bin` | 15000 |
| Arduino SDK3.4 | 14028 |
| ambiot/amb1_sdk | 16892 |
| pvvx RTL00_WEB | 8216 |

**Decompilation used as a reading aid.** pvvx's IDA decompile of a near-identical Image1 is `pvvx/rtl00TstMinAmebaV35a@463a3a70`, file `project/realtek_ameba1_va0_example/disasm_to_c_boot/startup345.c`:

- `PreProcessForVendor`: lines 472–703
- `IsForceLoadDefaultImg2`: lines 392–436

I checked the selection logic below instruction by instruction against the matching binary. The decompile agrees with it.

**What the local sources do and do not contain.**

- This checkout has no loader Ghidra reports and no loader source. `source/analysis/` holds only build and memory helpers. `recovery/third_party/rtl8710-source` is an OpenOCD flasher.
- The local SDK has only updater-side code: `system/component/soc/realtek/8195a/misc/platform/ota_8195a.c`, `system/component/common/application/xmodem/fw_update.c` and `libraries/OTA/OTA.cpp`.

## 1. How the loader chooses an Image2 slot

The code is `PreProcessForVendor`, the selection block at 0x100026FA–0x100028C4 [EV-loader]. Flash is memory-mapped at 0x98000000. The literal pool at 0x10002928 holds:

- `0x98000010`
- `0x9800000C`
- `"8195"` = 0x35393138
- `"0195"` = 0x35393130
- `"8711"` = 0x31313738
- `0x98000008`
- `0x98009000`
- `0x98000004`

The loader proceeds as follows.

1. **Default slot A.** Bits 0–15 of the word at flash 0x18, shifted left by 10 (0x1000270A–0x1000270C). The high bits are ignored, matching `sdk_ota_runtime.c:42`. On this device the value is 0x2C, so A = **0xB000**. If the field is 0, the loader uses `[0x10]+32` instead.
2. **Slot A candidacy.** The loader reads `A.len = [A+0]`. Then (0x1000272C–0x1000273E):
   - If `[A+8]=="8195"` **and** `[A+12]=="8711"`, it sets **NEW := A**.
   - Else if `[A+8]=="0195"` and `[A+12]=="8711"`, it sets **OLD := A**.
   - Otherwise A is not a candidate. This covers EMPTY, torn and garbage signatures.
3. **OTA slot B.** B is the word at `[0x9000]` (0x10002740).
   - If it is 0xFFFFFFFF, B is skipped.
   - B is valid only if `B >= A + A.len` in 32-bit arithmetic **and** B is 4 KiB-aligned (0x1000274E–0x1000275A). Otherwise the loader prints `"OTA addr … INVALID"` and skips B.
   - If B's signature is NEW, **NEW := B, overriding A**. If it is OLD, **OLD := B, overriding A** (0x10002770–0x10002786).
4. **Recovery pin.** `IsForceLoadDefaultImg2` (0x100024F0–0x1000256C) reads the bytes at 0x9008 and 0x9009. A byte of 0xFF is skipped. It returns `r7`, which is 1 only if a configured pin reads equal to its polarity bit (the last configured pin wins). With both bytes 0xFF it returns 0. [EV-loader] [EXT: amebaiot OTA page]
5. **Choice** (0x10002788–0x100027C6):
   - Not forced: NEW if any, else OLD.
   - Forced by the pin: OLD if any, else NEW.
   - Neither: prints `"Fatal:no fw"` and loops forever in the ROM console.
6. **Load and jump.**
   - The loader copies `len` bytes from `slot+16` to `[slot+4]`, which is 0x10006000 (0x10002814).
   - It probes an "Image3" header at `slot+16+len` (this image's checksum word) and the word after it. If the first is not 0 or FFFFFFFF and the second is 0x30000000, it hangs with "FW/HW conflict. No DRAM".
   - The only remaining check is `strcmp(load+4, "RTKWin")`. **There is no checksum or hash verification.** On mismatch it loops forever in the ROM console. **There is no fallback to the other slot** (0x1000285E–0x10002874).
7. **No flash writes in this path.** The selection block's callees are read-only: print, `RtlConsolRom`, the SPIC read routines at 0x10003376 and 0x1000324A, `strcmp`, and the GPIO helpers. **Nothing records boot attempts.** [EV-loader, scoped to this path] I did not audit the whole loader for flash writes.

**Corroboration.**

- The header layout matches the repo's checks: `len`, `0x10006000`, signature, entry, `"RTKWin"`. [EV-repo] `ota_guard.c:24-32`, `recovery/core.py:11`
- The SDK's default non-swap OTA sets NEW on the OTA slot and never demotes the default slot. [EV-sdk] `misc/platform/ota_8195a.h:13-14` (`SWAP_UPDATE 0`) and `ota_8195a.c:244-245,250-259`. That only works because a both-NEW state boots B.
- The recovery plan passes through (A NEW, B NEW) and then demotes B. [EV-repo] `recovery/core.py:63-76`

### Truth table

Assumes no recovery pin. A = 0xB000, B = 0x80000, and "other" means EMPTY or INVALID.

| A \ B | NEW | OLD | other |
|---|---|---|---|
| **NEW** | **B** (tie) | A | A |
| **OLD** | B | **B** (tie) | A (OLD) |
| **other** | B* | B* (OLD) | **FATAL** |

\* Only if `A + A.len ≤ 0x80000`. When A is erased, `len` is 0xFFFFFFFF; the sum wraps to A−1 and B stays valid. **If A's length word holds a garbage or partially-erased/partially-programmed value while A's signature is invalid, B can be rejected as well, which is FATAL.** Slot A's validity never depends on slot B. [EV-loader] + [INF about torn values]

## 2. What bit-clearing writes can and cannot do

**Available transitions without an erase:**

- **EMPTY → NEW.** Two word programs. Every torn intermediate is "other", because the target value never clears bit 3 of the `+8` word, so it cannot pass through OLD. [INF: standard NOR programming model]
- **NEW → OLD.** Clears exactly one bit, 0x38→0x30 in `+8`. **Atomic.**
- **NEW or OLD → other.** Clear bits of `+12` (0x31313738 in both). The first cleared bit makes the slot "other". Do **not** invalidate by zeroing `+8` from NEW: a torn write can pass through OLD.
- **Anything → NEW or OLD again** requires erasing the slot's first sector. While an erase is interrupted, bits return to 1 in an unpredictable subset. Partially erasing an OLD signature can therefore produce NEW, since NEW is OLD with bit 3 set. **Before erasing any sector that holds a valid signature, clear its `+12` and `+8` words to 0 first.** [INF]

**What the loader cannot provide.** The loader has no state. A reset re-selects the same slot unless the running image changes flash first. Rollback must be armed by the new image itself.

**Two NEW slots** always boot B. If B is a new image that hangs while A stays NEW, the device boot-loops into B. B must demote itself to arm rollback.

**Reachability by direction.** Each column below assumes the starting state listed in the first column.

| Direction | Starting state | Boot 1 → | Arm (new image) → | Confirm (new image) → |
|---|---|---|---|---|
| A→B | (A NEW, B other) | commit (A NEW, B NEW) → B | `B+8`→OLD: (A NEW, B OLD) → A | `A+8`→OLD: (A OLD, B OLD) → B. **No erase.** |
| A→B | (A OLD, B other) | commit (A OLD, B NEW) → B | `B+12`→0: (A OLD, B other) → A | B must win again, so **rewrite B's first sector** back to NEW. Every torn state is "B other" → A. **No FATAL path.** |
| B→A | (A other, B NEW or OLD) | commit (A NEW, B OLD) → A | `A+8`→OLD: (A OLD, B OLD) → B | `B+12`→0: (A OLD, B other) → A. **No erase.** |

The earlier draft of this analysis confirmed B→A by rewriting **A**'s header back to NEW. That adds a FATAL window, because a torn A length word can hide B (§1 footnote). The mirrored choice above never rewrites A's header outside the OTA download itself, so it is preferred.

## 3. Watchdog and boot-attempt recording

**The loader never enables or feeds the watchdog.** The only reference to the WDG register 0x40002800 is a read in an interrupt-handler routine at 0x100044FC–0x1000452A. [EV-loader]

**The firmware starts the watchdog late.** It enables it lazily from `mttl_sdk_watchdog_tick` → `wdt_enable(8000)` inside the worker loop. [EV-repo]

- `sdk_worker.c:51-66`
- The tick runs from the main loop, after Wi-Fi and relay setup: the `for(;;)` loop in `sdk_worker.c`, around line 301.
- The SDK caps the timeout at 8 s and uses reset mode. [EV-sdk] `cores/arduino/wiring_watchdog.h:30`; `WDGInitial` disassembly sets the 0x40 bit (`WdgMode=RESET_MODE`, `rtl8195a_wdt.h`)

**Unknown: whether the watchdog stays enabled through its own reset.** [INF, needs hardware] Practical consequences:

- A hang before the image enables the watchdog lasts until power is cycled.
- If the watchdog did survive, the loader copies about 330 KB well inside 8 s. [INF]

**Nothing records boot attempts.**

- The loader path writes neither flash nor a reset cause. [EV-loader]
- `HalSetResetCause`/`HalGetResetCause` use an indirect register through 0x40000094. [EV-sdk: `lib_platform.a:hal_misc.o` disassembly] Whether that register survives resets is unknown. **Do not rely on it.**
- The design below needs no counter.

## 4. Recommended design (mirrored variant)

### Notation

| Write | Meaning |
|---|---|
| `X+8=OLD8` | Program 0x35393130 at X+8. One bit is cleared. |
| `X+12=0` | Program 0x00000000 at X+12. Invalidates the slot. |
| `X:=NEW` | Program 0x35393138 at X+8, then 0x31313738 at X+12. |

### Trial marker

Place a magic word in the target slot's erased tail, at `target + file_len + 8` or later. **Never** use `target + file_len`, which the loader reads as the Image3 address; that word must stay erased.

- The old image writes `PENDING` at commit.
- The new image clears further bits to reach `CONFIRMED` after the confirm write.
- Treat anything other than the exact `PENDING` value as "not pending".
- The slot's next OTA erases the marker. The settings journal has no free sector for this. [EV-repo] `sdk_settings_flash.c:18`

The marker is required because each direction's boot-1 state equals a normal confirmed state. For example, (A OLD, B NEW) running B is both the boot-1 state of A→B and the canonical running-B state.

### Arming rule

Infer the running slot from the signatures using the truth table. This is valid only after checking that the loader is the fingerprinted one and that both bytes at 0x9008/0x9009 are 0xFF. **Add the 0x9008 check: today's probe does not read it**, and a configured pin inverts the NEW/OLD preference. Arm only if:

- the slot implied by the signatures is the slot holding `PENDING`, **and**
- the signature state is the expected boot-1 state.

`mttl_sdk_ota_probe` hashes about 400 KB. Use it only as a cross-check before confirming. In every trial state the two slots hold different valid images, so its `matches==1` requirement holds:

- `ota_identity.c:5-23`
- `same_image()` refuses identical uploads: `sdk_ota_runtime.c:83-100`

### Direction A→B (running A, target B)

| Step | By | Writes | State → boots | Power loss or hang here |
|---|---|---|---|---|
| 0 | A | If B has a valid signature: `B+12=0` and `B+8=0` (new). Then the existing erase, stream and readback. The erase runs bottom-up and the stream starts at offset 0 (`ota_stage.c:15-16,31-35`). | (A NEW or OLD, B other) → A | A. B's torn or erased header cannot hide A. |
| 1 | A | B marker = PENDING | → A | A |
| 2 | A | `B:=NEW`. Do **not** demote A. Then `sys_reset`. | A NEW: (A NEW, B NEW) → B. A OLD: (A OLD, B NEW) → B. | Torn writes leave B "other" → A (not applied) |
| 3 | B, first flash action after boot, before Wi-Fi, relays or settings | If A is NEW: `B+8=OLD8`. If A is OLD: `B+12=0`. Then enable the watchdog immediately. | → A in both cases | Hang before this write: stuck until power cycle, then step 3 runs again. After it: any reset → **A (rollback)**. |
| 4 | B | Health window | → A | Watchdog, crash, deadline reset or power loss → **A**. A sees B's marker still `PENDING` and B not selected, and reports the rollback. |
| 5 | B | If A is NEW: `A+8=OLD8` (atomic) → (A OLD, B OLD) → B. | → B | Before the bit is cleared → A. After → B. |
| 5′ | B | If A is OLD: rewrite B's first sector, steps (a)–(d) below. | → B | Every torn state → A (rollback; re-upload needed). No FATAL path. |

Step 5′ in detail:

- (a) Copy `B[0..4096)` to a scratch sector in B's own tail, in 256-byte chunks. Free heap is about 9.5 KB [EV-repo] `docs/validation-2.1.0.md:63`. B's capacity is 393216 bytes against a 332376-byte image [EV-repo] `docs/validation-2.1.1.md:25`. Verify the copy against the first-sector SHA recorded at commit.
- (b) `B+8=0` (B+12 is already 0). Erase sector 0x80000.
- (c) Program bytes 0..7 and 16..4095.
- (d) Read back and verify, then `B:=NEW` and set the marker to `CONFIRMED`.

### Direction B→A (running B, target A)

| Step | By | Writes | State → boots | Power loss or hang here |
|---|---|---|---|---|
| 0 | B | If A has a valid signature: `A+12=0` and `A+8=0` (new). Then erase, stream and readback of A. | (A other, B NEW or OLD) → B | **Existing, unavoidable hazard:** while A's first sector is being erased or its first page programmed, a torn A length word can make B invalid → **FATAL** (§1 footnote). This affects today's firmware too. Window: one sector-erase time (tens of ms, typical NOR) plus one page program. [INF timing] |
| 1 | B | A marker = PENDING | → B | B |
| 2 | B | `A:=NEW`, and if B is NEW, `B+8=OLD8`. Either order is safe. Then `sys_reset`. | (A NEW, B OLD) → A | All intermediate states → B |
| 3 | A, early | `A+8=OLD8`, then enable the watchdog | (A OLD, B OLD) → B | Hang before the write: stuck until power cycle, then repeat. After: any reset → **B (rollback)**. |
| 4 | A | Health window | → B | → **B**. B sees A's marker still `PENDING` and A OLD, and reports the rollback. |
| 5 | A | `B+12=0` → (A OLD, B other) → A. Then marker = `CONFIRMED`. | → A | The first cleared bit already invalidates B, so this is effectively atomic. |

### Resulting cycle

Starting from today's (A OLD, B NEW) running B:

1. B→A ends at (A OLD, B other), running A.
2. A→B with A OLD ends at (A OLD, B NEW), running B. Step 5′ applies here: one extra sector erase plus a scratch sector.
3. Starting from today's (A NEW, B OLD) running A, A→B ends at (A OLD, B OLD), running B, with no extra erase.

### Images without the protocol

This covers stock 1.0.66/1.0.68 restores (`legacy_image()`, `sdk_ota_runtime.c:22-31`) and standalone builds older than the trial version.

- Detect trial capability from a marker string in the image, as `ota_profile_present()` does (`sdk_ota_runtime.c:111-120`).
- For non-capable targets, keep the existing three-word commit: target NEW, active `+8=OLD8`.
- How stock firmware's own OTA behaves from (A OLD, B other) or (A OLD, B OLD) is **unknown** [INF]. That matters when a stock image runs after a restore.

**The first trial-capable build gets no protection.** Today's 2.1.x performs the commit for it. Protection starts with the update after that.

### Health and confirmation policy (product decision)

**Confirmation criteria:**

- Prefer local liveness for 60–120 s: the worker loop, the HTTP listener, the meter bus and a heap floor.
- Avoid requiring network reachability, so that a router outage does not trigger a rollback. A rollback would be harmless anyway.
- Optionally allow an explicit dashboard confirmation, in line with `docs/firmware-pull-client-plan.md:29`.

**During the window:**

- Block OTA, Wi-Fi-change reboots and settings migrations or saves. After a rollback the old image must still parse the settings. README warns that downgrades can clear schedules: `README_EN.md:167`, `:181`.
- Keep relay outputs as restored. Latching coils are not pulsed at boot: `sdk_worker.c:289`.

## 5. Risks

**Brick paths (FATAL / no firmware).**

- The only one is the pre-existing slot-A rewrite during a B→A download: a torn A length word while A has no valid signature (§1 footnote, §4 B→A step 0).
- The recommended design adds no new one. The earlier variant, which rewrote A's header at confirm, did add one.

**Interrupted erase can resurrect a signature.** An erase interrupted on a sector holding OLD could turn it into NEW over half-erased code. Mitigate by clearing `+12` and `+8` to 0 before every erase of a signature-bearing first sector. Today's OTA does not do this: `ota_stage.c:15-16` erases the target directly. Example: target B is OLD and active A is NEW. A torn erase that yields "B NEW" boots a half-erased B (both-NEW goes to B). [INF, low probability]

**Hang before the arm write.** Only a power cycle recovers it. Mitigations:

- Arm in the earliest code that can call the SDK flash API.
- Verify that `device_mutex_lock` (RT_DEV_LOCK_FLASH) can be used that early, or bypass it before the scheduler starts. [INF]

**Corruption after commit.** The loader does no checksum.

- A slot with a valid header and `RTKWin` that later crashes rolls back if it is already armed, and hangs if not.
- An `RTKWin` mismatch loops in the ROM console with no fallback and needs the debugger.
- The existing SHA readback before commit keeps this unlikely.

**Flash wear.**

- No extra erases, except one header sector plus one scratch sector in step 5′ (A→B when A is OLD).
- For comparison, the OTA itself erases about 80–100 sectors.
- Re-programming an already-programmed word with further bit clears is already used today (NEW→OLD), and web OTA has been repeated on the device: `README_EN.md:147`. Check the flash datasheet for any limit on partial-page programs. [INF]

**Probe ambiguity.** The probe needs exactly one matching slot (`sdk_ota_runtime.c:57-73`, `ota_identity.c:5-23`).

- This holds in every trial state.
- After a rolled-back A→B, B is OLD or other but holds a different image, so the probe stays unique.
- The probe's eligibility rule (NEW or OLD, `sdk_ota_runtime.c:68`) rejects a slot whose `+12` was zeroed. That is correct, because such a slot cannot be the running one.

**Invariants in today's code that must change.** [EV-repo]

- `commit_word` whitelists only target+8, target+12 and active+8 (`sdk_ota_runtime.c:102-110`). The design needs:
  - direction-specific commits;
  - the new writes `+12=0` and `+8=0`;
  - the step-5′ sector rewrite;
  - marker writes.
- The commit model has exactly three fixed steps (`ota_guard.c:38-52`).
- The plan and erase rounding must reserve tail space for the marker and the scratch sector (`ota_guard.c:12-23`).
- An early-boot arm hook and an earlier watchdog start are needed.
- `/api` status, which reports `active_slot` at `sdk_web.c:152`, should add trial and rollback state.
- Host and ARM mocks need the full state table, including torn words and interrupted erases.

## 6. Evidence quality

| Claim | Basis |
|---|---|
| Selection order and tie-breaks; "other" slots are invisible; no checksum; no fallback after `RTKWin`; no flash writes on the selection path; recovery-pin semantics | [EV-loader], from a binary SHA-identical to the device fingerprint |
| Device values at 0x18 (low half 0x2C) and 0x9000 (0x80000) | [EV-repo], runtime checks in `boot_compatible()` and `sdk_settings_flash.c:28-29` |
| Device bytes at 0x9008/0x9009 | **Unknown** |
| Watchdog state after reset; torn or partial-erase bit patterns; erase timing; how early the flash API can be used; stock firmware's OTA behaviour from the new states | [INF] |

## 7. Must be validated on hardware (recovery debugger attached)

1. Read 0x18, 0x9000 and 0x9008–0x900B. Confirm 0x9008/0x9009 = FF FF. Re-confirm the loader fingerprint.
2. With log UART attached, observe `load NEW/OLD fw` and its OTA-slot flag for these states:
   - (NEW,NEW) → B
   - (NEW,OLD) → A
   - (OLD,OLD) → B
   - (OLD,NEW) → B
   - (OLD,other) → A
   - (other,NEW) → B
3. Arm-write timing: the flash API and mutex can be used before the network and relays, and arming happens within a bounded time after reset.
4. Watchdog: a deliberately hung trial image is reset and the *other* slot boots. Also check whether the watchdog is still enabled after that reset.
5. Power-cut tests at every step in both directions. Include:
   - an interrupted erase of slot A's first sector and an interrupted program of its first page, which is the existing FATAL hazard;
   - an interrupted erase of a sector that holds a valid signature, with and without zeroing it first;
   - interruptions during step 5′.
6. Step 5′: the scratch copy, erasing B's first sector while executing from SRAM, readback, and the final signature write.
7. Stock 1.0.66/1.0.68 booting from the new canonical states, and their own OTA from those states.
8. Settings compatibility across a rollback: the trial image must not migrate or save before it confirms.

## Sources

- [Ameba Arduino `ram_1.r.bin`, SHA-identical to the device loader (commit 1619ba43)](https://github.com/ambiot/amb1_arduino/blob/dev/Arduino_package/ameba_tools_linux/bsp/image/ram_1.r.bin)
- [pvvx `startup345.c` Image1 decompile (commit 463a3a70)](https://github.com/pvvx/rtl00TstMinAmebaV35a/blob/master/project/realtek_ameba1_va0_example/disasm_to_c_boot/startup345.c)
- [Amebaiot: OTA update program to Ameba (system data 0x9000/0x9008, recovery pin)](https://www.amebaiot.com/ameba-arduino-ota/)
