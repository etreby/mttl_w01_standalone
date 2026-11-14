# Local test results — 2026-10-15

## 1.0.0 — 2026-10-16

The user confirmed successful builder operation, OTA installation of the generated FWR and normal operation on their device. This confirms that device/workflow, not every hardware variant or modification. Version 1.0.0 retains the tested build engine and adds release documentation.

0.1.1 adds Korean/English UI switching and folder-local language preferences. Translation and engine unit tests: 12 passed. Compiler/SDK raw diagnostics remain unchanged. GUI visual layout verification remains incomplete as described below.

- Official upstream download, pinned SHA-256 verification and local extraction: passed.
- Embedded Python 3.12.10, Node 22.14.0, GCC 4.8.3, SDK and package probes: passed.
- Source without complete flash dumps or manufacturer firmware: FWR generation passed.
- Packaged EXE CLI: environment check plus full build passed (exit 0).
- Engine unit tests: 11 passed, including archive traversal/link rejection, bad download hash, cancellation, exclusive locking, environment isolation and failed-build output suppression.
- GUI process/window starts. Visual capture returned black and activation was denied; GUI visual layout and mouse interactions are **not fully verified**. Please test the visible interface yourself.
- No firmware uploaded and no GitHub changes made.

The newly downloaded Windows compiler differs from the historical mixed local toolchain. Its FWR is 327756 bytes (historical 2.0.12 release: 327740). Both compile/link and packaging checks pass; do not describe this as a byte-identical or target-tested firmware. Actual uploaded device behavior is not yet verified for the builder-generated file.

The firmware source ZIP omits the original-decoder fixture check to avoid distributing the original firmware; format, checksum, hardware field, BIN/FWR roundtrip and setup ARM mock checks remain. Existing SDK enum ABI warnings are retained in the log and have not been resolved by this builder project.

This package is for local testing, not a finalized public redistribution release. Third-party licensing and fresh-PC validation remain separate work. Tools download on first prepare; no toolchain/SDK archive is embedded in the builder ZIP. Build and download caches accumulate only in the builder home. The EXE is unsigned.
