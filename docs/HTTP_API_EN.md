# Local HTTP control API

[Project](../README_EN.md)

This documents the API used by the dashboard in the current public firmware, 2.1.1. Local programs can call it without an MQTT broker. This documentation update does not change firmware behavior.

## Requirements

- The strip must be connected to the router and online. Web relay commands are rejected in SoftAP mode.
- Use `http://<the strip's current IPv4 address>`. Addresses below are examples.
- There is no authentication token; the `Origin` check is not authentication. Use only on a trusted LAN and do not expose the port to the internet.
- This is not a CORS API for other websites. Run the browser example in the strip dashboard's developer console. External programs must supply the `Origin` header themselves.

## POST /api/control

Send **exactly two binary bytes** with `Content-Type: application/octet-stream`. Do not send JSON, the text `01`, the text `[0,1]`, or form data.

| Byte offset | Meaning | Allowed values |
|---|---|---|
| 0 | Channel | `0`=channel 1, `1`=channel 2, `2`=channel 3, `3`=channel 4, `4`=all |
| 1 | Action | `0`=OFF, `1`=ON |

The required header is `Origin: http://<current IPv4 address>`. Do not include a path, trailing slash or `:80`. The address must match the target strip's current address.

### Python example — standard library

```python
import urllib.request

base = 'http://192.168.0.153'  # Replace with your strip's IP
request = urllib.request.Request(
    base + '/api/control',
    data=bytes([0, 1]),  # Channel 1 ON; all OFF is bytes([4, 0])
    headers={'Content-Type': 'application/octet-stream', 'Origin': base},
    method='POST',
)
with urllib.request.urlopen(request, timeout=10) as response:
    print(response.status, response.read().decode())
```

### JavaScript example — strip dashboard developer console

```javascript
const response = await fetch('/api/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/octet-stream'},
  body: new Uint8Array([0, 1]) // Channel 1 ON
});
console.log(response.status, await response.text());
```

The browser supplies `Origin` for the POST. Running this example on another website or from a local HTML file is not supported.

## Responses and status checks

| HTTP status | Meaning |
|---|---|
| `200` | Command accepted into the control queue; not confirmation of completed relay switching |
| `400` | Missing/mismatched `Origin`, or body length is not two bytes |
| `429` | Rejected: invalid channel/action, full queue, offline state, OTA/configuration work, hardware/relay fault, etc. |
| `405` | Unsupported method other than GET or POST |

Successful response:

```json
{"accepted":true,"contact_feedback":false}
```

`contact_feedback:false` means this is not a result verified using physical contact feedback. Like dashboard/HA commands, these commands are unaffected by physical-button locks; protection cutoffs, timers and other normal behavior still apply.

- `GET /api/device`: logical channel states (`on`) and measurements. `on:null` means unknown. This does not guarantee the physical contact position.
- `GET /api/status`: network and metering diagnostics.
- `GET /api/control`: control diagnostics (`sequence`, `wait_ms`, `run_ms`, `busy`) only; no relay switching. No per-request completion acknowledgement ID is provided.

The public firmware does not currently provide `/api/relay?ch=1&state=1` URL-based control. A request timeout does not prove the command was not accepted: check state rather than retrying blindly. Avoid flooding or unlimited retries.

## Firmware upload: POST /api/firmware/upload

Uploads a `.fwr` file into the inactive slot and, only after every check passes, activates it and reboots. The dashboard Firmware tab uses this endpoint.

| Header | Required | Value |
|---|---|---|
| `Origin` | yes | `http://<current IPv4 address>` |
| `Content-Type` | yes | `application/x-mttl-fwr` |
| `Content-Length` | yes | Exact `.fwr` size (44 to 434196 bytes) |
| `X-MTTL-SHA256` | no | 64 hexadecimal characters (either case): SHA-256 of the **raw `.fwr` file exactly as uploaded**, i.e. the value `sha256sum file.fwr` prints and an OTA server publishes |

The body is the raw `.fwr` file. The whole request header block must fit in 1024 bytes; send `X-MTTL-SHA256` once (a duplicated header line is treated as absent).

`X-MTTL-SHA256` behaviour:

- **Absent:** unchanged behaviour; only the internal format/checksum/receive/readback/slot checks apply.
- **Malformed** (not exactly 64 hex digits): `400 Malformed X-MTTL-SHA256; no write`, returned before any flash erase.
- **Present:** the device hashes every received byte before decoding. If the digest differs, the session is aborted before `activate`, the running image stays active and the response is `400 SHA-256 mismatch; nothing activated`. The inactive slot may contain partially written data, which is never booted.

`GET /api/firmware/status` reports `"sha256_header":true` on firmware that supports this header.

```python
import hashlib, urllib.request
base = 'http://192.168.0.153'
data = open('comMTTL-W01_2.1.1.fwr', 'rb').read()
request = urllib.request.Request(base + '/api/firmware/upload', data=data, method='POST', headers={
    'Origin': base, 'Content-Type': 'application/x-mttl-fwr',
    'X-MTTL-SHA256': hashlib.sha256(data).hexdigest()})  # or the value published by your OTA server
print(urllib.request.urlopen(request, timeout=180).read().decode())
```

Success responses are `{"written":true,"activated":true,"rebooting":true}` (optionally with `restore_version`) or `{"already_installed":true,"activated":false,"rebooting":false}`.

## Pull update: /api/pull/*

Opt-in update from a LAN OTA server with an Ed25519-signed manifest (OTA
firmware only; contract in [PULL_UPDATE_EN.md](PULL_UPDATE_EN.md)). Nothing is
checked or installed without one of the POSTs below. Every POST needs
`Origin: http://<current IPv4 address>`, like the other POSTs.

| Request | Body | Result |
|---|---|---|
| `GET /api/pull/status` | — | Status JSON (below) |
| `POST /api/pull/base` | empty (disable), or 6 bytes: IPv4 then port, both big endian | `200 {"accepted":true,"pending":true}`; the worker saves it to flash. `400` unless 0 or 6 bytes, or not an RFC 1918 address with a non-zero port; `429` while busy |
| `POST /api/pull/check` | empty | Fetches `GET /v1/device/manifest` (≤ 15 s), verifies it and returns the status JSON. `429` when no server is set or a check/install is running |
| `POST /api/pull/install` | the 16 hex `build_id` shown by the last check | `200 {"accepted":true,"pending":true}` is sent **before** the download; the device then downloads, verifies and activates the image and reboots (≤ 180 s + reboot). `400` when nothing matching and unexpired is on offer |

During a check or install the device answers other HTTP requests only after
it finishes. After an install, poll `GET /api/firmware/status` until
`build_id` changes; if the device answers with the old `build_id`, read
`GET /api/pull/status` for the error (no automatic retry).

`GET /api/config` includes `"pull_base":"http://192.168.0.10:8080"` (`""` when
disabled). `POST /api/config` bodies never change it.

Status JSON:

```json
{"state":3,"error":0,"rom_ready":1,"version":"2.2.0","build_id":"0123456789abcdef","size":434000,"sha256":"…64 hex…","expires_at":1900000000}
```

`state` and `error` are numeric (table in
[PULL_UPDATE_EN.md](PULL_UPDATE_EN.md#device-check-order)). `rom_ready` is the
signature verifier's one-time self-test: 0 not run yet, 1 passed, -1 failed
(then every check fails with `signature`). The offered fields appear only for a
manifest whose signature verified.

```python
import json, urllib.request
base = 'http://192.168.0.153'
def post(path, body=b''):
    return urllib.request.urlopen(urllib.request.Request(base + path, data=body, method='POST', headers={'Origin': base}), timeout=30).read()
post('/api/pull/base', bytes([192, 168, 0, 10]) + (8080).to_bytes(2, 'big'))
offer = json.loads(post('/api/pull/check'))
print(offer)  # install only after reviewing it:
# post('/api/pull/install', offer['build_id'].encode())
```
