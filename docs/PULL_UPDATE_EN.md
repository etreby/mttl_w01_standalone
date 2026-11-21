# Pull update check: manifest contract

Status: **contract and host-tested logic only.** The device-side HTTP fetch,
install handler and dashboard buttons are **not** in the firmware image yet
(see [What is implemented](#what-is-implemented)). Nothing here starts an
update on its own.

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
retries by itself.

Error codes: `malformed`, `signature`, `expired`, `time` (clock not
synchronised), `not_newer`, `size`, `http`, `redirect`.

## What is implemented

Implemented and covered by the host self-test
(`source/standalone/run_host_selftest.sh`):

- `firmware/ed25519_verify.c`: verify-only Ed25519 derived from public-domain
  TweetNaCl; RFC 8032 section 7.1 TEST 1, 2, 3, 1024 and SHA(abc) plus
  negative cases (altered R/S/message, other key, `S + L`, off-curve key,
  zero signature) and SHA-512 known answers.
- `firmware/pull_update.c`: base-URL validation, request builder, response
  head checks (redirects refused), strict manifest parser, canonicalisation,
  policy checks and the explicit-action state machine, tested against
  manifests produced by `pull_manifest.py`.

Not implemented on the device, because it does not fit the current RAM image
(the whole image, code included, runs from SRAM; after the PIN feature only
276 bytes remain before the MSP reserve):

| Component | Cortex-M3 `-Os` size |
|---|---|
| `ed25519_verify.c` | 5,660 bytes code/constants + 4,928 bytes static workspace (can live in TCM) |
| `pull_update.c` | 2,821 bytes |
| Socket client, `/api/update/check` and `/api/update/install` handlers, settings field, dashboard buttons | not written; estimated 1.5–2.5 KB |

Worst-case verification stack from `-fstack-usage`: about 0.85 KB (SHA-512
block function 496 bytes under the 304-byte verifier frame), within the 4 KB
httpd stack. Verification time on the device has not been measured.

The RTL8710 ROM exports `rom_ed25519_verify_signature`,
`rom_ed25519_crypto_sign_verify_detached` and `rom_sha512*` (see the SDK's
`export-rom_v03.txt`), which would remove most of the 5.6 KB. The SDK ships
no prototypes for them and the ROM cannot be executed in this environment,
so they are not used: a guessed calling convention must never gate firmware
activation. Using them would need a known-answer self-test (one valid and one
invalid RFC 8032 vector) before every use, and hardware validation.

Mbed TLS 2.16.1 in the SDK has no EdDSA (only Curve25519 ECDH), so it cannot
provide this verification.
