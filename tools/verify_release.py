#!/usr/bin/env python3
"""Verify a downloaded MTTL-W01 firmware release.

Checks, in this order:
  1. SHA256SUMS-<version>.txt.sig is a valid Ed25519 signature (base64, 64 bytes)
     over the exact bytes of SHA256SUMS-<version>.txt, made by the key whose public
     half is compiled into the firmware (source/standalone/firmware/pull_key.h).
  2. Every file listed in SHA256SUMS exists next to it and matches its SHA-256.
  3. The list names exactly one .fwr, and no unlisted .fwr sits in the folder.

Nothing listed in SHA256SUMS is trusted before the signature verifies.
Requires Python 3.8+ and the `cryptography` package. Never contacts a device.

usage: tools/verify_release.py [--dir DIR] [--pubkey HEX | --pull-key PATH]
Exit status 0 only when every check passes.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import re
import sys
from pathlib import Path

DEFAULT_KEY_HEADER = Path(__file__).resolve().parents[1] / 'source/standalone/firmware/pull_key.h'
SUMS_NAME = re.compile(r'^SHA256SUMS-(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.txt$')
SUMS_LINE = re.compile(r'^([0-9a-f]{64}) [ *]([A-Za-z0-9._-]+)$')


class VerifyError(Exception):
    pass


def public_key_from_header(path: Path) -> bytes:
    """The 32-byte key from MTTL_PULL_PUBLIC_KEY; the hex comment must agree."""
    text = path.read_text(encoding='utf-8')
    array = re.search(r'#define\s+MTTL_PULL_PUBLIC_KEY\s*\{([^}]*)\}', text)
    if not array:
        raise VerifyError(f'{path}: MTTL_PULL_PUBLIC_KEY not found')
    key = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', array.group(1)))
    if len(key) != 32:
        raise VerifyError(f'{path}: MTTL_PULL_PUBLIC_KEY is not 32 bytes')
    comment = re.search(r'hex:\s*([0-9a-f]{64})', text)
    if comment and bytes.fromhex(comment.group(1)) != key:
        raise VerifyError(f'{path}: hex comment and byte array disagree')
    return key


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def verify(folder: Path, public_key: bytes) -> list[tuple[str, str]]:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    sums_files = sorted(p for p in folder.iterdir() if SUMS_NAME.match(p.name))
    if len(sums_files) != 1:
        raise VerifyError(f'expected exactly one SHA256SUMS-<version>.txt in {folder}, found {len(sums_files)}')
    sums = sums_files[0]
    sig_path = sums.with_name(sums.name + '.sig')
    if not sig_path.is_file():
        raise VerifyError(f'missing signature {sig_path.name}')
    sums_bytes = sums.read_bytes()
    try:
        signature = base64.b64decode(sig_path.read_text(encoding='ascii').strip(), validate=True)
    except (binascii.Error, UnicodeDecodeError, ValueError):
        raise VerifyError(f'{sig_path.name} is not base64') from None
    if len(signature) != 64:
        raise VerifyError(f'{sig_path.name} must decode to 64 bytes, got {len(signature)}')
    try:
        Ed25519PublicKey.from_public_bytes(public_key).verify(signature, sums_bytes)
    except InvalidSignature:
        raise VerifyError(f'BAD SIGNATURE on {sums.name} for public key {public_key.hex()}') from None

    try:
        lines = sums_bytes.decode('ascii').splitlines()
    except UnicodeDecodeError:
        raise VerifyError(f'{sums.name} is not ASCII') from None
    entries: list[tuple[str, str]] = []
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        match = SUMS_LINE.match(line)
        if not match:
            raise VerifyError(f'{sums.name}:{number}: malformed line')
        entries.append((match.group(2), match.group(1)))
    names = [name for name, _ in entries]
    if len(set(names)) != len(names):
        raise VerifyError(f'{sums.name} lists a file more than once')
    version = SUMS_NAME.match(sums.name)
    version_text = '.'.join(version.groups())
    fwr = [n for n in names if n.endswith('.fwr')]
    if len(fwr) != 1:
        raise VerifyError(f'{sums.name} must list exactly one .fwr, lists {len(fwr)}')
    if not re.fullmatch(r'comMTTL-W01_' + re.escape(version_text) + r'-build-[0-9a-f]{16}\.fwr', fwr[0]):
        raise VerifyError(f'{fwr[0]} does not match comMTTL-W01_{version_text}-build-<16 hex>.fwr')
    unlisted = sorted(p.name for p in folder.glob('*.fwr') if p.name not in names)
    if unlisted:
        raise VerifyError('unlisted firmware in folder: ' + ', '.join(unlisted))
    for name, expected in entries:
        path = folder / name
        if not path.is_file():
            raise VerifyError(f'missing listed file {name}')
        actual = sha256_file(path)
        if actual != expected:
            raise VerifyError(f'SHA-256 MISMATCH for {name}: expected {expected}, got {actual}')
    return entries


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dir', type=Path, default=Path('.'), help='folder with the release assets (default: current)')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--pubkey', help='trusted Ed25519 public key as 64 hex characters')
    group.add_argument('--pull-key', type=Path, default=DEFAULT_KEY_HEADER, help='pull_key.h to read the key from')
    args = parser.parse_args(argv)
    try:
        if args.pubkey:
            if not re.fullmatch(r'[0-9a-fA-F]{64}', args.pubkey):
                raise VerifyError('--pubkey must be 64 hex characters')
            key = bytes.fromhex(args.pubkey)
        else:
            if not args.pull_key.is_file():
                raise VerifyError(f'{args.pull_key} not found; pass --pubkey or --pull-key')
            key = public_key_from_header(args.pull_key)
        entries = verify(args.dir.resolve(), key)
    except VerifyError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    print(f'signature OK (Ed25519 public key {key.hex()})')
    for name, digest in entries:
        print(f'OK  {digest}  {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
