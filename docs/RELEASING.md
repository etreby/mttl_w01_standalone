# Releasing firmware

Releases of the English-only 2.2.x line are built, signed and published by
GitHub Actions (`.github/workflows/release.yml`). Nobody uploads a hand-built
`.fwr`. The workflow never contacts a device and never flashes anything.

## One-time setup: the signing secret

The workflow signs `SHA256SUMS-<version>.txt` with Ed25519. The key must be
the one whose public half is compiled into the firmware as the pull-update
trust anchor, `source/standalone/firmware/pull_key.h`
(`d3711a8c…ceb1bd`). `tools/verify_release.py` checks the signature against
that header, so a release signed with any other key fails before it is
published.

Create the repository secret **`MTTL_RELEASE_SIGNING_KEY`** under
*Settings → Secrets and variables → Actions*. Its value is the **base64 of the
raw 32-byte Ed25519 seed** (44 characters ending in `=`), not hex and not PEM.
If the seed is stored as 64 hex characters, convert it locally without writing
it to disk or shell history, for example:

```sh
python3 -c 'import base64,sys;print(base64.b64encode(bytes.fromhex(sys.stdin.read().strip())).decode())' < seed.hex
```

The job fails if the secret is missing, not base64 or not 32 bytes. The
secret is read only in the signing step and is never printed.

Rotating the key means changing `pull_key.h`, releasing that firmware, and
updating the secret; older releases stay verifiable only with the old key
(`tools/verify_release.py --pubkey <old hex>`).

## Cutting a release

1. Bump the version in **`source/mttl-source.json`** and
   **`source/standalone/firmware/version.h`** (`MTTL_FIRMWARE_VERSION`), the
   static preview strings in `source/standalone/ui/index.html` and
   `ui/ota-test.html`, the titles of `source/README*.md`, and add a
   `### <version>` entry to the version history in `README.md` and
   `README_EN.md`. `source/standalone/tests/test_version.py` fails if any of
   these disagree; the device build (`build_network_test.py`) also refuses to
   build when `version.h` and `mttl-source.json` differ.
2. Run locally: `source/standalone/run_host_selftest.sh`, the node tests in
   `source/standalone/tests/`, `python3 source/standalone/tests/test_version.py`
   and a build with `python3 builder/linux_build.py --source source --cache <dir>`.
   The reported static gap to the MSP reserve must stay at or above 4096 bytes.
3. Merge to `main`, then tag the merge commit and push the tag:

   ```sh
   git tag -a v2.2.0 -m "MTTL-W01 2.2.0"
   git push origin v2.2.0
   ```

   The tag must be `v` + the exact version in `mttl-source.json`. To re-run a
   failed release for an existing tag, use *Actions → release → Run workflow*
   with that tag (delete a partially created release first).

## What the workflow does

1. Validates the tag (`vMAJOR.MINOR.PATCH`) and checks it equals the firmware version.
2. Restores the cached builder downloads/environment and the mbedTLS sources
   for the host self-test (cache key: `builder/dependencies.lock.json` and
   `builder/linux_build.py`). All builder downloads, including the Linux GCC
   archive, are SHA-256 pinned.
3. Runs the host self-test, the node tests, the minifier test, the version
   consistency test and the verifier's own tests.
4. Builds with `builder/linux_build.py` and checks the manifest: version,
   16-hex build ID, file name, BIN/FWR round trip, OTA write enabled and a
   static gap of at least 4096 bytes beyond the 4096-byte MSP reserve.
5. Re-runs the host self-test's real-image upload checks and the browser FWR
   inspector against the freshly built `.fwr`.
6. Writes `SHA256SUMS-<version>.txt`, signs it, and runs
   `tools/verify_release.py --dir dist` against `pull_key.h`.
7. Creates the GitHub release with `gh` and the job's `GITHUB_TOKEN`
   (`contents: write` for this job only). The build is not bit-reproducible:
   the build ID is random per build, so every run produces a different image.

## Release assets

| Asset | Meaning |
| --- | --- |
| `comMTTL-W01_<version>-build-<build id>.fwr` | The firmware, for the device's Firmware tab or the initial-installation OTA tool. It carries the `NETWORK_OTA_TEST_RAM_SETTINGS_<build id>` marker that the OTA server's import requires. The build ID is what `GET /api/firmware/status` reports after installation, and the dashboard remembers it from this file name to confirm the update. |
| `comMTTL-W01_<version>-build-<build id>.manifest.json` | Builder manifest: version, build ID, ELF/BIN/FWR SHA-256, static gap to MSP, validation flags. Paths in it are runner paths and have no meaning outside the build. |
| `SHA256SUMS-<version>.txt` | `sha256sum` format, one line each for the `.fwr` and the manifest. |
| `SHA256SUMS-<version>.txt.sig` | Base64 of the 64-byte detached Ed25519 signature (RFC 8032, pure) over the exact bytes of the SHA256SUMS file. |

Users verify with:

```sh
pip install cryptography
python3 tools/verify_release.py --dir <folder with all four assets>
```

The script needs a checkout of the repository (it reads `pull_key.h`), or
`--pubkey <64 hex>` / `--pull-key <path>`.

## How the OTA server consumes a release

The OTA server's GitHub import (`ota_server/github.py` in
`mttl-w01-ota-server`, enabled with `OTA_GITHUB_IMPORT=1`, repositories in
`OTA_GITHUB_REPOS`) lists `.fwr` assets of non-draft releases and downloads
one only when an operator asks. It takes the expected SHA-256 from the digest
GitHub publishes for the asset, or, if absent, from the release's
`SHA256SUMS*.txt` asset, and refuses the import when neither exists or the
download does not match. The version label comes from the asset name
(`comMTTL-W01_<version>-build-…`). The imported file then goes through the
server's own firmware validation (FWR format, size cap `0x6a014`, activation
marker) and needs explicit approval before any device sees it.

The server does not currently check `SHA256SUMS-<version>.txt.sig`; the
signature is for people and tools verifying downloads, and the same key signs
pull-update manifests on the server. Do not rename assets: the `.fwr` name
pattern and the `SHA256SUMS-<version>.txt` name are what the import matches.
