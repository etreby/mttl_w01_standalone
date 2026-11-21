# Firmware pull-client integration plan

Status: **plan only**. Do not flash a device or start an unattended update.

> Superseded in part by [PULL_UPDATE_EN.md](PULL_UPDATE_EN.md): the device cannot afford TLS, so the manifest contract uses plain HTTP on the LAN with an Ed25519-signed manifest and the signed SHA-256 of the image instead of HTTPS.

## Fork baseline

- Repository: `etreby/mttl_w01_standalone`
- Branch: `english-dashboard-safe-controls`
- Head reviewed: `cb49be3289a91609f9c7da40bf417144381a4a08` (`Give dashboard distinct dark layout update previews`)
- Published firmware remains the upstream 2.1.1 image. The branch documentation states that the redesigned dashboard is not included in that image and has not passed physical-device validation.

## Existing update path

The fork already contains a **device-hosted push OTA** path:

- The dashboard checks `GET /api/firmware/status`.
- The dashboard uploads a selected `.fwr` file to `POST /api/firmware/upload`.
- The upload uses `application/x-mttl-fwr`, and the UI waits for reconnect after the device reboots.
- The firmware-side OTA modules provide format/header validation, staged writes, SHA/readback checks, and a commit state machine.
- `ota-test.html` is a test-only page that additionally requires a local 32-character OTA token.

This is not yet a pull client. There is no firmware-side manifest fetch, remote image download, signature verification, or automatic update scheduler in the reviewed branch.

## Proposed safe integration

1. **Add an explicit opt-in configuration.** Keep pull updates disabled by default. Require a user action in the dashboard to enable the feature and to select the update channel or manifest URL. Do not add a background or boot-time update trigger in the first implementation.
2. **Use a strict manifest.** Require HTTPS, a pinned/allowed host, an exact semantic version, image byte length, SHA-256, and a detached signature. Reject redirects, unsupported content types, oversized images, downgrades, and images that do not match the device hardware and boot layout.
3. **Reuse the existing OTA session.** Stream the response body into the existing staged OTA APIs. Do not buffer the complete image in RAM. Preserve the existing active-slot, geometry, write, readback, and commit checks.
4. **Separate download from activation.** Download and verify into the inactive slot, expose a pending-update status, and require an explicit dashboard confirmation before commit/reboot. A failed connection, hash, signature, or readback must leave the active image unchanged.
5. **Add transport and failure limits.** Set connection, header, idle, total-duration, redirect, retry, and byte limits. Use bounded backoff only after explicit user enablement. Never retry indefinitely and never update over the setup AP.
6. **Add observability without secrets.** Report state (`disabled`, `checking`, `downloading`, `verifying`, `pending-confirmation`, `failed`) and a short error code. Do not log URLs containing credentials, tokens, or response bodies.
7. **Test before hardware.** Add host tests for manifest parsing, signature and hash failures, truncation, oversized content, redirects, timeout/retry bounds, downgrade rejection, interrupted writes, simulated power loss, and inactive-slot preservation. Run the existing OTA/self-test suite and static checks. Physical-device validation is a separate approved step.

## Approval gates

- No unattended update behavior.
- No physical flash during planning or software-only validation.
- No replacement of the published 2.1.1 firmware with the dashboard preview build.
- Before hardware testing, obtain approval for the manifest host, signing-key handling, update channel, rollback behavior, and test device.
