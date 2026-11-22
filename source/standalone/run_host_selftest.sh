#!/bin/sh
# Build and run the firmware's portable self-test on a Linux/macOS host.
# No device, SDK toolchain, flash or network access to the strip is involved:
# flash/Wi-Fi/DHCP use the in-tree mocks (MTTL_SDK_FLASH_TEST, MTTL_SDK_WIFI_TEST).
# ota_hash.c compiles the vendor mbedTLS 2.16.1 sha256.c; it is fetched from the
# official Mbed TLS release tarball into a cache directory when missing.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
FW="$HERE/firmware"
CACHE=${MTTL_HOST_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/mttl-w01-host}
CC=${CC:-cc}
MBED="$CACHE/mbedtls-mbedtls-2.16.1"
OUT="$CACHE/build"
mkdir -p "$CACHE" "$OUT"
if [ ! -f "$MBED/library/sha256.c" ]; then
  echo "Fetching mbedTLS 2.16.1 (sha256.c and headers only)..." >&2
  curl -fsSL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/mbedtls-2.16.1.tar.gz |
    tar xz -C "$CACHE" 'mbedtls-mbedtls-2.16.1/library/sha256.c' 'mbedtls-mbedtls-2.16.1/include/mbedtls'
fi
sed "s#\.\./\.\./work/reference/ameba-arduino-1/Arduino_package/hardware/system/component/common/network/ssl/mbedtls-2.16.1/library/sha256.c#$MBED/library/sha256.c#" \
  "$FW/ota_hash.c" > "$OUT/ota_hash_host.c"
cat > "$OUT/main.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
extern uint32_t mttl_selftest(void);
extern uint32_t mttl_test_count, mttl_test_failed, mttl_failed_ids[64];
extern void mttl_ota_upload_file_test(const char *, const char *, const char *);
int main(int argc, char **argv) {
  uint32_t failed = mttl_selftest(), i;
  if (argc == 4) { mttl_ota_upload_file_test(argv[1], argv[2], argv[3]); failed = mttl_test_failed; }
  printf("firmware selftest: %u checks, %u failed\n", (unsigned)mttl_test_count, (unsigned)failed);
  for (i = 0; i < failed && i < 64; i++) printf("  failed check #%u\n", (unsigned)mttl_failed_ids[i]);
  return failed != 0;
}
EOF
SOURCES=$(cd "$FW" && ls *.c | grep -v -e '^sdk_' -e '^ota_hash\.c$')
EXTRA="sdk_flash_mock.c sdk_ota_flash.c sdk_wifi_mock.c sdk_dhcp.c sdk_wifi_adapter.c"
cd "$FW"
# shellcheck disable=SC2086
$CC -std=gnu11 -O1 -w -fno-builtin -DMTTL_SDK_FLASH_TEST -DMTTL_SDK_WIFI_TEST -I. -I"$MBED/include" \
  $SOURCES $EXTRA "$OUT/ota_hash_host.c" "$OUT/main.c" -o "$OUT/selftest"
# Real .fwr: expected digests come from sha256sum and the Python reference
# decoder at test time, never from constants in the C code. Defaults to the
# historical upstream 2.1.1 fixture; MTTL_TEST_FWR selects another image (the
# release workflow points it at the freshly built release .fwr).
FWR=${MTTL_TEST_FWR:-"$HERE/tests/fixtures/comMTTL-W01_2.1.1.fwr"}
FWR_SHA=$(sha256sum "$FWR" | cut -d' ' -f1)
DECODED_SHA=$(cd "$HERE/.." && python3 -I -c 'import hashlib,sys;sys.path.insert(0,".");from standalone.ota import inspect_image;print(hashlib.sha256(inspect_image(open(sys.argv[1],"rb").read()).decoded).hexdigest())' "$FWR")
"$OUT/selftest" "$FWR" "$FWR_SHA" "$DECODED_SHA"
