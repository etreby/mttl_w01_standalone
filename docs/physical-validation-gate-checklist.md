# MTTL-W01 Physical Validation Gate — Review-Only Checklist

**Purpose:** Prepare a controlled acceptance record for a later physical validation run. This document authorizes no hardware action by itself.

## Hard gate

- [ ] Firmware remains **2.1.1**, build `33231c0032fa013b`, SHA-256 `362f92df30f16fadcfd012ba20557ce58466f08706e99bca6cf6e1f67ae1ce9f`.
- [ ] Owner has explicitly approved this specific run, device, operator, date, and test scope below.
- [ ] No flash, relay, mains, or wiring action occurs before the approval is recorded.
- [ ] Planned downtime and affected loads have been identified and accepted.

**Owner approval (required before execution):**

- Owner: ____________________
- Approval date/time: ____________________
- Device identifier / MAC: ____________________
- Approved scope: ____________________
- Approval reference/comment: ____________________

## Pre-flight — power-off and workspace safety

- [ ] Device is isolated from mains before opening, probing, or changing wiring.
- [ ] Connected loads are identified; hazardous, motor, heating, or unattended loads are removed or disconnected unless explicitly in scope.
- [ ] Relay/load safety plan is documented; no load is switched without an operator watching the result.
- [ ] Electrical isolation, enclosure condition, insulation, earth/ground, and probe ratings are verified by the operator.
- [ ] Only the approved power source, USB/debug adapter, and cables are present; polarity and voltage are checked with power off before connection.
- [ ] A second person or independent check is present for mains re-energization, if required by site practice.

## Backup and recovery readiness

- [ ] Recovery image(s) are available from a trusted source and match the approved target/version.
- [ ] Device-specific recovery procedure is ready; generic backups or another device's dump are prohibited.
- [ ] Two independent pre-change dumps are captured and stored in a private, access-controlled run folder.
- [ ] Dump sizes, hashes, device identity, and read timestamps are recorded.
- [ ] The two dumps match byte-for-byte; mismatch means **abort — no write**.
- [ ] Recovery image hash and provenance are recorded.
- [ ] Backup folder is not uploaded to issues, chat, or shared storage because dumps may contain credentials and device-specific data.
- [ ] A rollback operator, rollback image, and stop point are named before execution.

## Power-loss and abort criteria

Abort immediately and do not improvise a retry if any of the following occurs:

- power, USB, debug, or communication becomes unstable;
- device, adapter, cable, or load becomes hot, emits odor/smoke, arcs, or behaves unexpectedly;
- voltage, isolation, wiring, or polarity differs from the approved setup;
- pre-flight hashes, image identity, or readback verification do not match;
- the tool reports write failure, readback failure, backup mismatch, protected-region violation, or an unknown state;
- mains or load state cannot be observed safely;
- the owner-approved scope changes.

On abort: stop writes, preserve logs and both original dumps, leave the device in the tool-reported safe state, and escalate to the owner. Do not force-close the tool or arbitrarily power-cycle during an active write/readback operation.

## Controlled execution record (complete only after approval)

- Operator: ____________________
- Observer: ____________________
- Start/end time: ____________________ / ____________________
- Downtime window: ____________________
- Device identity: ____________________
- Starting firmware/build: ____________________
- Approved target image/build: ____________________
- Pre-change dump 1 hash: ____________________
- Pre-change dump 2 hash: ____________________
- Recovery image hash: ____________________
- Tool/version: ____________________
- Loads disconnected / test load: ____________________

### Before action

- [ ] Owner approval rechecked immediately before action.
- [ ] Device is in the documented safe state.
- [ ] Before-image/version/configuration/diagnostic dump captured.
- [ ] Relay states and connected-load state recorded.
- [ ] No unexplained diagnostic or protection fault is present.

### During action

- [ ] Power and communications remained continuously available.
- [ ] Tool logs were captured without editing.
- [ ] No relay/load switching occurred outside the approved sequence.
- [ ] Any warning or deviation was recorded and the run paused.

### After action

- [ ] Full readback verification passed.
- [ ] Post-image/version/build recorded.
- [ ] After dump captured and hashed.
- [ ] Device reboot completed without forced power cycling.
- [ ] Expected setup/LAN behavior and dashboard availability checked.
- [ ] Relay logical state and actual contact/load behavior checked separately.
- [ ] No unexpected heat, noise, odor, or protection event observed.
- [ ] Planned load test completed within approved limits.
- [ ] Logs, hashes, timestamps, and deviations attached by reference (not sensitive dumps).

## Acceptance decision

**Pass only if:** owner-approved scope completed; backups agree; write/readback verification passes; device boots; expected firmware/build is confirmed; safety observations are normal; and all deviations are resolved or explicitly accepted by the owner.

- Result: `PASS / CONDITIONAL / FAIL / ABORT`
- Deviation(s): ____________________
- Rollback required: `YES / NO`
- Rollback result and verification: ____________________
- Owner final disposition: ____________________
- Owner signature/date: ____________________

## Rollback record

Rollback is mandatory when the owner directs it, the device fails the approved acceptance criteria, or any safety/identity/readback abort criterion is met.

- [ ] Loads isolated and device placed in safe state.
- [ ] Correct device-specific recovery image selected.
- [ ] Original verified dumps and logs preserved.
- [ ] Rollback write completed with full readback verification.
- [ ] Reboot and expected recovery firmware confirmed.
- [ ] Post-rollback functional and safety checks completed.
- [ ] Owner notified of result; no further flash attempt without a new approval.

**Rollback notes:** ____________________
