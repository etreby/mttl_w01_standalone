# Third-party notices

This builder is for local Windows x64 use. It makes no system installation, global PATH change, registry registration or automatic device upload. Tools and selected sources live in a dedicated folder. Build scripts can run arbitrary code, so build only trusted sources.

**Prepare environment** downloads the pinned components below directly from their upstream distribution sites and verifies SHA-256. It does not run an installer, npm, pip or a system package manager. It rejects archive links and paths outside the extraction folder. Download servers may log ordinary connection details such as IP addresses. The builder does not upload source or device settings.

## Downloaded components

- **GNU Arm Embedded GCC 4.8.3-2014q1:** Arduino's distribution server. GCC/Binutils GPL terms, library exceptions and component licenses apply. Using the compiler does not by itself put all user source under the GPL.
- **Ameba Arduino SDK:** Ameba-AIoT commit `819d5a3252a4294847dc035bc431b1437dddbee3`. The builder downloads the official SDK release and file overlays from the same commit. Its components include Arduino LGPL, FreeRTOS 8.1.2 GPL with exception, Realtek notices and other terms. Review those terms when using the SDK or distributing modified firmware.
- **Python 3.12.10 embeddable:** python.org; PSF and bundled component licenses.
- **Node.js 22.14.0:** nodejs.org; MIT and bundled component licenses.
- **Unicorn 2.1.4:** upstream PyPI wheel; GPLv2 engine and separate binding terms.
- **Zopfli 0.4.3:** PyPI; Apache-2.0 and component notices.
- **Terser 5.44.0 and pinned dependencies:** npm registry; each package's BSD/MIT or other license applies.

Exact URLs, versions, sizes and SHA-256 digests are in `dependencies.lock.json`. Retain original download archives and extracted license files in the cache. Downloading dependencies separately does not remove license obligations for a resulting firmware distribution.

## Vendored source

- **TweetNaCl 20140427** (Daniel J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers): public domain, https://tweetnacl.cr.yp.to/. `source/standalone/firmware/ed25519_verify.c` is a modified verify-only subset (SHA-512, field arithmetic, Ed25519 verification) of `tweetnacl.c` (original SHA-256 `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4`). Modifications are listed in the file header. It is currently compiled only into the host self-test, not into the device firmware.

## Builder EXE

The EXE is a PyInstaller one-directory package containing Python/Tcl/Tk. Retain its bundled `licenses` folder and license files under `_internal`. Python uses PSF terms, Tcl/Tk their own notices, and PyInstaller GPL with its bootloader exception. The EXE is an unsigned test build; review Windows or security-product warnings before running it.

This document is not legal advice. Publishing this project's source does not relicense third-party components. Verify redistribution terms separately for modified firmware.
