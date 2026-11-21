"""Reference signer for the pull-update manifest (docs/PULL_UPDATE_EN.md).

Run on the OTA server / release machine, never on the strip. Requires the
`cryptography` package. The 32-byte Ed25519 seed is read from a file as 64
hex characters; keep it offline and out of the repository.

  python3 pull_manifest.py pubkey --key seed.hex
  python3 pull_manifest.py sign --key seed.hex --fwr comMTTL-W01_2.2.0.fwr \
      --version 2.2.0 --build-id 0123456789abcdef --url /v1/device/firmware/2.2.0.fwr --ttl 86400
  python3 pull_manifest.py verify --pubkey <64 hex> < manifest.json
"""
import argparse, base64, hashlib, json, re, sys, time

FIELDS = ('build_id', 'expires_at', 'sha256', 'size', 'url', 'version')
URL = re.compile(r'^/(?!.*//)(?!.*\.\.)[A-Za-z0-9._~/-]{0,127}$')


def canonical(manifest):
    """Signed bytes: the six non-signature members, sorted keys, no spaces."""
    body = {k: manifest[k] for k in FIELDS}
    check(body)
    return json.dumps(body, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')


def check(m):
    assert re.fullmatch(r'[0-9a-f]{16}', m['build_id']), 'build_id: 16 lowercase hex'
    assert re.fullmatch(r'[0-9a-f]{64}', m['sha256']), 'sha256: 64 lowercase hex'
    assert re.fullmatch(r'(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})', m['version']), 'version: MAJOR.MINOR.PATCH'
    assert URL.match(m['url']), 'url: absolute path, [A-Za-z0-9._~/-], no // or ..'
    for k in ('size', 'expires_at'):
        assert type(m[k]) is int and 0 <= m[k] <= 0xffffffff, k
    assert 44 <= m['size'] <= 0x6a014, 'size: 44..434196'


def load_key(path):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    return Ed25519PrivateKey.from_private_bytes(bytes.fromhex(open(path).read().strip()))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    k = sub.add_parser('pubkey'); k.add_argument('--key', required=True)
    s = sub.add_parser('sign')
    for name in ('--key', '--fwr', '--version', '--build-id', '--url'):
        s.add_argument(name, required=True)
    s.add_argument('--ttl', type=int, default=86400)
    s.add_argument('--expires-at', type=int)
    v = sub.add_parser('verify'); v.add_argument('--pubkey', required=True)
    a = p.parse_args()
    if a.cmd == 'pubkey':
        from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
        print(load_key(a.key).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex())
    elif a.cmd == 'sign':
        data = open(a.fwr, 'rb').read()
        m = {'build_id': a.build_id, 'expires_at': a.expires_at or int(time.time()) + a.ttl,
             'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'url': a.url, 'version': a.version}
        m['signature'] = base64.b64encode(load_key(a.key).sign(canonical(m))).decode()
        print(json.dumps(m, sort_keys=True, separators=(',', ':')))
    else:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        m = json.load(sys.stdin)
        assert set(m) == set(FIELDS) | {'signature'}, 'exactly the seven members'
        Ed25519PublicKey.from_public_bytes(bytes.fromhex(a.pubkey)).verify(base64.b64decode(m['signature'], validate=True), canonical(m))
        print('signature OK')


if __name__ == '__main__':
    main()
