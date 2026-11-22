"""Offline tests for tools/verify_release.py (needs the `cryptography` package)."""
from pathlib import Path
import base64
import hashlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_release as vr  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: E402
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat  # noqa: E402

FWR = 'comMTTL-W01_2.2.0-build-0123456789abcdef.fwr'
MANIFEST = 'comMTTL-W01_2.2.0-build-0123456789abcdef.manifest.json'


class VerifyRelease(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.key = Ed25519PrivateKey.generate()
        self.pub = self.key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        (self.dir / FWR).write_bytes(b'firmware' * 100)
        (self.dir / MANIFEST).write_text('{"version":"2.2.0"}')
        self.write_sums()

    def tearDown(self):
        self.tmp.cleanup()

    def write_sums(self, names=(FWR, MANIFEST)):
        lines = ''.join(f'{hashlib.sha256((self.dir / n).read_bytes()).hexdigest()}  {n}\n' for n in names)
        sums = self.dir / 'SHA256SUMS-2.2.0.txt'
        sums.write_text(lines)
        (self.dir / 'SHA256SUMS-2.2.0.txt.sig').write_text(base64.b64encode(self.key.sign(sums.read_bytes())).decode() + '\n')

    def test_ok(self):
        self.assertEqual([n for n, _ in vr.verify(self.dir, self.pub)], [FWR, MANIFEST])

    def test_wrong_key(self):
        other = Ed25519PrivateKey.generate().public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        with self.assertRaisesRegex(vr.VerifyError, 'BAD SIGNATURE'):
            vr.verify(self.dir, other)

    def test_tampered_sums(self):
        sums = self.dir / 'SHA256SUMS-2.2.0.txt'
        sums.write_text(sums.read_text().replace(MANIFEST, MANIFEST + 'x'))
        with self.assertRaisesRegex(vr.VerifyError, 'BAD SIGNATURE'):
            vr.verify(self.dir, self.pub)

    def test_tampered_firmware(self):
        data = bytearray((self.dir / FWR).read_bytes()); data[10] ^= 1
        (self.dir / FWR).write_bytes(bytes(data))
        with self.assertRaisesRegex(vr.VerifyError, 'MISMATCH'):
            vr.verify(self.dir, self.pub)

    def test_unlisted_firmware(self):
        (self.dir / 'comMTTL-W01_2.1.1.fwr').write_bytes(b'x')
        with self.assertRaisesRegex(vr.VerifyError, 'unlisted'):
            vr.verify(self.dir, self.pub)

    def test_missing_signature(self):
        (self.dir / 'SHA256SUMS-2.2.0.txt.sig').unlink()
        with self.assertRaisesRegex(vr.VerifyError, 'missing signature'):
            vr.verify(self.dir, self.pub)

    def test_requires_one_fwr(self):
        self.write_sums(names=(MANIFEST,))
        with self.assertRaisesRegex(vr.VerifyError, 'exactly one .fwr'):
            vr.verify(self.dir, self.pub)

    def test_repository_key_header(self):
        key = vr.public_key_from_header(vr.DEFAULT_KEY_HEADER)
        self.assertEqual(len(key), 32)


if __name__ == '__main__':
    unittest.main()
