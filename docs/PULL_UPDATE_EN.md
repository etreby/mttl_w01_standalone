# Pull update check: manifest contract

Status: **implemented in the OTA firmware image, host-tested, not yet
validated on hardware** (see [What is implemented](#what-is-implemented) and
[Hardware-only checks](#hardware-only-checks)). Nothing here starts an update
on its own.

## Principles

- **Opt-in, explicit actions only.** Disabled while no OTA base URL is
  configured. A check runs only when the user presses *Check for update*;
  installation runs only after a second explicit *Install* click that names
  the `build_id` the dashboard displayed. No timers, no boot-time checks,
  no automatic install or retry.
- **Plain HTTP on the LAN, authenticity from a signature.** The device has no
  TLS budget (about 10 KB free heap). Authenticity comes from an Ed25519
  signature over a canonical manifest, verified on the device against a
  public key compiled into the firmware. Integrity of the image comes from
  the signed `sha256`, enforced by the same check as `X-MTTL-SHA256`
  ([HTTP API](HTTP_API_EN.md#firmware-upload-post-apifirmwareupload)).
- **Same install path.** The image is streamed through the same decoder,
  OTA session, readback and activation rules as `POST /api/firmware/upload`;
  the raw-file SHA-256 is compared before activation.

## OTA base URL

`http://<IPv4>:<port>` with an optional trailing `/`, nothing else: no host
names, no credentials, no path, no query. The IPv4 address must be in an
RFC 1918 range (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`); the port
is required (1–65535). Example: `http://192.168.0.10:8080`.

It is empty (pull disabled) by default and set in the dashboard Firmware tab
or with `POST /api/pull/base`; `GET /api/config` reports it as `pull_base`
([HTTP API](HTTP_API_EN.md#pull-update-apipull)).

## Requests made by the device

Both requests are `HTTP/1.0` (so responses are never chunked), carry no
credentials or cookies, and are sent only after an explicit user action:

```http
GET /v1/device/manifest HTTP/1.0
Host: 192.168.0.10:8080
Accept: */*
Connection: close
```

and, after *Install*, `GET <url from the manifest>` to the same host and port.

Response rules (anything else is a failure, never retried automatically):

- Status line `HTTP/1.0 200` or `HTTP/1.1 200`. **3xx responses are never
  followed**; other statuses fail.
- `Content-Length` is required; `Transfer-Encoding` is rejected. The header
  block must fit in 1024 bytes.
- Manifest body: at most 1024 bytes.
- Image body: `Content-Length` must equal the manifest `size`.
- Recommended device limits: 5 s connect, 10 s idle, 15 s total for the
  manifest, 180 s total for the image.

## Manifest

`Content-Type: application/json`. A single JSON object with **exactly** these
seven members (unknown or duplicate members are rejected):

| Member | Type | Rule |
|---|---|---|
| `version` | string | `MAJOR.MINOR.PATCH`, each 0–999, no leading zeros. Must be newer than the running version (2.1.1). |
| `build_id` | string | 16 lowercase hex characters (as in `GET /api/firmware/status`). |
| `size` | integer | Exact `.fwr` file size, 44–434196 (`0x6a014`). |
| `sha256` | string | 64 lowercase hex: SHA-256 of the `.fwr` file exactly as served. |
| `url` | string | Absolute path on the same server, starts with `/`, at most 128 characters from `A-Z a-z 0-9 . _ ~ / -`, no `//` and no `..`. |
| `expires_at` | integer | Unix time in seconds (UTC), 0–4294967295. Rejected when `expires_at <= now`; rejected when the device clock is not NTP-synchronised. |
| `signature` | string | Standard base64 with padding (88 characters) of the 64-byte Ed25519 signature. |

Strings may not contain escapes or characters outside printable ASCII;
integers are unsigned decimal without sign, fraction, exponent or leading
zeros. Whitespace between tokens and member order are free.

### Canonical signed message

The signature covers the UTF-8/ASCII bytes of the six non-signature members
serialised with **keys sorted ascending, no whitespace**, strings verbatim and
integers in decimal:

```text
{"build_id":"…","expires_at":N,"sha256":"…","size":N,"url":"…","version":"…"}
```

This is exactly Python `json.dumps(body, sort_keys=True, separators=(',', ':'))`
for a body that satisfies the rules above. Signature: Ed25519 (RFC 8032, pure,
no prehash, no context) over those bytes. The device rejects non-canonical
`S` (`S >= L`).

### Example

Signed with the public RFC 8032 TEST 1 key (`d75a9801…f707511a`); **never use
that key in production**:

```json
{"build_id":"0123456789abcdef","expires_at":1900000000,"sha256":"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2","signature":"n86eaG/Vf6hCRE7HhTA93OCKO2i3gC+hKwRqjLT3nRBl76v7X7P2C7tv2YZ+mcnm5XBJEdGnBhGo8UNbsBBNAQ==","size":1000,"url":"/v1/device/firmware/comMTTL-W01_2.2.0.fwr","version":"2.2.0"}
```

Signed bytes:

```text
{"build_id":"0123456789abcdef","expires_at":1900000000,"sha256":"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2","size":1000,"url":"/v1/device/firmware/comMTTL-W01_2.2.0.fwr","version":"2.2.0"}
```

### Reference signer

`source/standalone/pull_manifest.py` (needs the Python `cryptography` package)
produces and checks manifests. Keep the 32-byte seed offline; only the public
key is compiled into the firmware.

```sh
python3 source/standalone/pull_manifest.py pubkey --key seed.hex
python3 source/standalone/pull_manifest.py sign --key seed.hex --fwr comMTTL-W01_2.2.0.fwr \
    --version 2.2.0 --build-id <16 hex> --url /v1/device/firmware/comMTTL-W01_2.2.0.fwr --ttl 86400
python3 source/standalone/pull_manifest.py verify --pubkey <64 hex> < manifest.json
```

## Ed25519 on the device: mask-ROM verifier

Device builds (`-DMTTL_ED25519_ROM`) verify with the chip's mask-ROM export
`rom_ed25519_crypto_sign_verify_detached` (`export-rom_v03.txt`, 0x1d655;
libsodium-style prototype as in Realtek's `rom_homekit_ed25519.h`) through
`firmware/ed25519_rom.c`, instead of linking the bundled TweetNaCl verifier
(`ed25519_verify.c`, kept for host builds and as the test reference).

- Size: 433 bytes (wrapper, known-answer vectors, state) instead of 5660 bytes
  of code plus 4928 bytes of static workspace.
- Because the ROM ABI is undocumented for this chip, the ROM verifier is
  trusted only after a one-time known-answer test (RFC 8032 TEST 1 must verify
  and a one-bit-tampered copy must not). It runs lazily on the first
  verification (an explicit user action), never at boot. If it fails, every
  verification returns invalid and pull checks stay disabled.
- `S >= L` is rejected before the ROM is called.
- The trusted public key is `firmware/pull_key.h`.
- Not yet validated on hardware: ROM call ABI, stack use inside the ROM, and
  verification time.

## Device check order

1. Parse strictly (bounded, no escapes, exactly seven members).
2. Rebuild the canonical message and verify the Ed25519 signature. **Nothing
   in the manifest is used before this succeeds.**
3. Require a synchronised clock and `expires_at > now`.
4. Require `44 <= size <= 434196` and `version` newer than the running one.
5. Report `available` with `version`, `build_id`, `size` and `sha256` to the
   dashboard; nothing is downloaded yet.

States: `disabled` (no base URL) → `idle` → `checking` → `available` |
`current` (not newer) | `failed` (error code); `available` → `installing`
only on an explicit *Install* naming the displayed `build_id` while the
manifest is still unexpired; a failed install returns to `failed` and never
retries by itself. Changing or clearing the base URL resets the state, so a
manifest verified from one server is never installed from another.

`GET /api/pull/status` reports them as numbers (`firmware/pull_update.h`):

| `state` | | `error` | |
|---|---|---|---|
| 0 | `disabled` | 0 | none |
| 1 | `idle` | 1 | `malformed` |
| 2 | `checking` | 2 | `signature` (also: verifier unavailable, see `rom_ready`) |
| 3 | `available` | 3 | `expired` |
| 4 | `current` | 4 | `time` (clock not synchronised) |
| 5 | `failed` | 5 | `not_newer` |
| 6 | `installing` | 6 | `size` (manifest size, or image `Content-Length` ≠ `size`) |
| | | 7 | `http` (connect/send/receive, timeouts, status, framing, limits) |
| | | 8 | `redirect` |
| | | 9 | `state` |
| | | 10 | `digest`: image SHA-256 ≠ signed `sha256`; nothing activated |
| | | 11 | `install`: owner hold, geometry/running slot, flash or activation failed |

## What is implemented

Implemented and covered by the host self-test
(`source/standalone/run_host_selftest.sh`, 2343 checks):

- `firmware/ed25519_verify.c`: host reference verifier derived from public-domain
  TweetNaCl; RFC 8032 section 7.1 TEST 1, 2, 3, 1024 and SHA(abc) plus
  negative cases. **Never linked into the device image.**
- `firmware/ed25519_rom.c`: mask-ROM wrapper with its known-answer gate,
  tested with TweetNaCl standing in for the ROM and with broken verifiers.
- `firmware/pull_update.c`: base-URL validation, request builder, response
  head checks (redirects refused), strict manifest parser, canonicalisation,
  policy checks and the explicit-action state machine, tested against
  manifests produced by `pull_manifest.py`.
- `firmware/pull_client.c`: the HTTP/1.0 client and the check flow over
  socket callbacks: one connection, request bytes, head read byte by byte up
  to the blank line (≤ 1024 bytes, so no body byte is consumed with it),
  `Content-Length` required, `Transfer-Encoding` and 3xx refused, manifest
  body ≤ 1024 bytes, 5 s connect, 10 s idle, 15 s manifest and 180 s image
  totals, `GET /api/pull/status` JSON. Tests use a scripted socket with a fake
  clock (any chunking, partial sends, early close, stall, slow drip, refused
  connect, tick wrap-around).
- Install streaming: the real `firmware/comMTTL-W01_2.1.1.fwr` is served over
  the scripted socket and streamed by `mttl_pull_read` through
  `mttl_ota_upload_stream` (the loop shared with `POST /api/firmware/upload`)
  in ≤ 256-byte chunks; the raw SHA-256 must equal the signed value and the
  decoded image must equal the Python reference decoder's. A flipped byte
  fails verification; an early close fails the stream.
- Settings: the base URL is stored as IPv4 + port (`firmware/settings.c`).
  Without a URL the flash record is the unchanged version-3 record,
  byte-identical to what earlier firmware wrote (fixture test). With a URL the
  record uses tag `0x84` (version 3 + 6 bytes). Web (version-7) configuration
  bodies never carry or clear it. Older firmware cannot read tag `0x84`:
  **clear the OTA server URL before downgrading.**

On the device (OTA build profile, `MTTL_OTA_TEST`):

- **Where the client runs.** Check and install run synchronously inside their
  POST handlers on the single httpd thread, exactly like
  `POST /api/firmware/upload`: the OTA session, the energy/owner hold and the
  HTTP scratch buffer already have that single owner, and the owner hold
  waits for the worker task, so the install cannot run on the worker. The
  worker also owns relays, protection and MQTT and has a 30 s watchdog, so it
  must not block for 15–180 s. While a check (≤ 15 s) or install (≤ 180 s)
  runs, other HTTP requests wait; nothing else is blocked.
- **Where the signature is verified.** The ROM verifier's stack use is
  unknown (ref10-style, estimated 2.5–3.5 KB) and the httpd stack is 4 KB, so
  `mttl_ed25519_verify` hands the verification (and the lazy known-answer
  test) to the 6 KB worker stack and waits ≤ 5 s for it to be picked up.
- **Install** answers the POST first (`202`), then: GET the signed `url`,
  require `Content-Length == size`, and run the same install function as the
  upload (owner hold, geometry/running-slot check, stream into the OTA
  session, raw SHA-256 compare against the signed `sha256`, activation,
  reboot after 1 s). Failures abort the session, release the hold and set
  `failed` with `digest`, `install`, `size` or `http`.
- **Buffers.** Check: head + manifest share one 1024-byte buffer, plus 384
  bytes of verify scratch, inside the existing 1792-byte HTTP scratch union
  (TCM); no heap allocation. Install: decoder state + 1024-byte head buffer,
  reused for 256-byte chunks, in the same union. Persistent pull state: 232
  bytes in TCM. Socket: the MQTT plaintext adapter (`sdk_mqtt_socket.c`).
- **Memory budget** (OTA image, `builder/linux_build.py`): static gap to the
  MSP reserve 1368 bytes (was 6504 before the pull client). To fit, own
  statics `published`, `life`, `scan_cache`, the OTA `session` and the button
  inputs moved from main RAM to the TCM `.mttl.cache` section, which
  `setup()` now zeroes before any task starts (the build's setup emulator
  checks this); the TCM newlib tail shrinks from 16504 to 15112 bytes.
- Dashboard (Firmware tab): OTA server URL field, *Check for update*,
  offered version/build/size/expiry, and a separate *Install* button behind a
  `confirm()`; progress and reconnect reuse the upload's reconnect logic.

### Hardware-only checks

- ROM `rom_ed25519_crypto_sign_verify_detached`: call ABI, stack use on the
  worker, run time, and that the known-answer test passes (`rom_ready: 1`).
- lwIP: one more TCP socket while SSE, MQTT and NTP sockets are open; behaviour
  with the device in setup-AP mode (the check simply fails with `http`).
- End-to-end install from a real LAN server, reboot into the new image,
  dashboard reconnect; failure paths with a wrong image and a pulled cable.
- Statics moved to TCM (GPIO button state, Wi-Fi scan cache, OTA session,
  worker status/lifecycle): normal operation after the move.
- Runtime heap: four more page registrations (about 16–20 bytes each).
- The expiry check trusts the unauthenticated SNTP clock: an attacker who can
  spoof NTP *and* replay an old signed manifest could get an expired (but
  validly signed, newer) image offered; the signature still pins the image.
