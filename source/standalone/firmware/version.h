#ifndef MTTL_VERSION_H
#define MTTL_VERSION_H
/* Firmware version reported by /api/version, /api/firmware/status, Home
 * Assistant discovery ("sw") and the image's version string. Must equal
 * source/mttl-source.json "version"; build_network_test.py and
 * tests/test_version.py enforce that. 2.2.x is the English-only fork line;
 * upstream's Korean releases end at 2.1.1. */
#define MTTL_FIRMWARE_VERSION "2.2.1"
#endif
