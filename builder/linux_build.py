#!/usr/bin/env python3
"""Linux port of MTTL-Builder's "prepare tools" + "build firmware" steps.

Uses the same pinned SDK release, SDK file overrides, terser npm packages and
unicorn/zopfli versions as builder/dependencies.lock.json (SHA-256 verified),
plus Arduino's Linux x64 build of the same GCC 4.8.3-2014q1 the Windows builder
uses. Never contacts a device and never flashes anything: it only produces a
.fwr and manifest under <cache>/output/<build-id>/ for manual review.

usage: builder/linux_build.py [--source DIR] [--cache DIR]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import uuid
import venv
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
LOCK = json.loads((HERE / "dependencies.lock.json").read_text())
GCC_LINUX = {
    "url": "https://downloads.arduino.cc/gcc-arm-none-eabi-4.8.3-2014q1-linux64.tar.gz",
    # Not in the Windows lock (Windows builds use a different archive); pinned here so a
    # fresh environment (e.g. the release workflow) never trusts an unverified toolchain.
    "sha256": "d23f6626148396d6ec42a5b4d928955a703e0757829195fa71a939e5b86eecf6",
}
PY_PINS = {"unicorn": "2.1.4", "zopfli": "0.4.3"}


def log(message: str) -> None:
    print(f"[linux-build] {message}", flush=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, target: Path, expected: str | None) -> Path:
    if target.is_file() and (expected is None or sha256(target) == expected):
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_suffix(target.suffix + ".part")
    log(f"download {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "MTTL-Linux-Builder/1.0"})
    with urllib.request.urlopen(request, timeout=60) as response, part.open("wb") as out:
        shutil.copyfileobj(response, out)
    if expected and sha256(part) != expected:
        part.unlink()
        raise SystemExit(f"SHA-256 mismatch for {url}")
    part.replace(target)
    return target


def safe_parts(name: str) -> tuple[str, ...]:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or any(p in ("..", "") for p in path.parts):
        raise SystemExit(f"unsafe archive path: {name}")
    return path.parts


def extract(archive: Path, dest: Path, strip: int, keep=lambda parts: True, allow_links: bool = False) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    root = dest.resolve()
    with tarfile.open(archive) as tar:
        for info in tar:
            parts = safe_parts(info.name)[strip:]
            if not parts or not keep(parts) or info.isdir():
                continue
            target = dest.joinpath(*parts)
            if info.issym() or info.islnk():
                if not allow_links:
                    raise SystemExit("links inside archives are not supported")
                # Toolchain archives use relative links; keep them only if they stay inside dest.
                link = info.linkname if info.issym() else str(dest.joinpath(*safe_parts(info.linkname)[strip:]))
                resolved = (target.parent / link).resolve() if info.issym() else Path(link).resolve()
                if not resolved.is_relative_to(root):
                    raise SystemExit(f"archive link escapes destination: {info.name}")
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.is_symlink() or target.exists():
                    target.unlink()
                if info.issym():
                    target.symlink_to(link)
                else:
                    os.link(resolved, target)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(info) as src, target.open("wb") as out:
                shutil.copyfileobj(src, out)
            target.chmod(info.mode & 0o755 | 0o600)


def prepare(cache: Path) -> Path:
    env_dir = cache / "env"
    ready = env_dir / "ready.json"
    key = hashlib.sha256(json.dumps([LOCK, GCC_LINUX, PY_PINS], sort_keys=True).encode()).hexdigest()
    if ready.is_file() and json.loads(ready.read_text()).get("key") == key:
        return env_dir
    stage = cache / f".prepare-{uuid.uuid4().hex[:8]}"
    downloads = cache / "downloads"
    for item in LOCK["artifacts"]:
        kind, url, expected = item["kind"], item["url"], item.get("sha256")
        if kind in ("gcc", "python", "node", "wheel"):
            continue  # Windows binaries; replaced below.
        archive = download(url, downloads / expected[:16] / Path(url).name, expected)
        if kind == "sdk_release":
            extract(archive, stage / "sdk", strip=1)
        elif kind == "sdkfile":
            target = (stage / "sdk").joinpath(*safe_parts(item.get("relative") or item["name"].removeprefix("sdk/")))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(archive, target)
        elif kind == "npm":
            extract(archive, stage / "js/node_modules" / item.get("package", item["name"]), strip=1)
    gcc_archive = download(GCC_LINUX["url"], downloads / "gcc-linux" / Path(GCC_LINUX["url"]).name, GCC_LINUX["sha256"])
    (cache / "tools").mkdir(parents=True, exist_ok=True)
    (cache / "tools/gcc.sha256").write_text(sha256(gcc_archive) + "  " + gcc_archive.name + "\n")
    extract(gcc_archive, stage / "gcc", strip=1, allow_links=True)
    # build scripts call "<tool>.exe"; expose Linux binaries under those names.
    shim = stage / "gcc-shim/bin"
    shim.mkdir(parents=True)
    for tool in (stage / "gcc/bin").iterdir():
        (shim / (tool.name + ".exe")).symlink_to(tool)
    venv.EnvBuilder(with_pip=True, clear=True).create(stage / "venv")
    subprocess.run([str(stage / "venv/bin/pip"), "install", "-q", *[f"{k}=={v}" for k, v in PY_PINS.items()]], check=True)
    (stage / "ready.json").write_text(json.dumps({"key": key, "created": time.time()}))
    if env_dir.exists():
        shutil.rmtree(env_dir)
    stage.replace(env_dir)
    # Shim links point into the stage directory name; re-point them after the rename.
    for link in (env_dir / "gcc-shim/bin").iterdir():
        target = env_dir / "gcc/bin" / link.name.removesuffix(".exe")
        link.unlink()
        link.symlink_to(target)
    log(f"environment ready: {env_dir}")
    return env_dir


def build(source: Path, cache: Path, env_dir: Path) -> Path:
    if not (source / "mttl-source.json").is_file():
        raise SystemExit("--source must contain mttl-source.json")
    build_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
    work = cache / "build" / build_id
    shutil.copytree(source, work, ignore=shutil.ignore_patterns("__pycache__", "results", ".git", "tools", "downloads", "output", "build"))
    sdk = work / "work/reference/ameba-arduino-1/Arduino_package/hardware"
    sdk.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(env_dir / "sdk", sdk)
    node = shutil.which("node")
    if not node:
        raise SystemExit("node is required on PATH")
    env = {k: v for k, v in os.environ.items() if k.upper() not in {"PYTHONHOME", "PYTHONPATH", "VIRTUAL_ENV", "NODE_PATH"}}
    env.update(PATH=os.pathsep.join([str(env_dir / "venv/bin"), str(env_dir / "gcc-shim/bin"), str(env_dir / "gcc/bin"), os.path.dirname(node), "/usr/bin", "/bin"]),
               MTTL_GCC_DIR=str(env_dir / "gcc-shim"), MTTL_TERSER_ROOT=str(env_dir / "js/node_modules/terser"),
               PYTHONDONTWRITEBYTECODE="1", TMPDIR=str(work / "temp"))
    (work / "temp").mkdir()
    log(f"building in {work}")
    subprocess.run([str(env_dir / "venv/bin/python"), str(work / "build.py")], cwd=work, env=env, check=True)
    out = work / "standalone/results/network-ota-test"
    manifest = json.loads((out / "manifest.json").read_text())
    fwr = next(x for x in manifest["files"] if x["path"].endswith(".fwr"))
    path = Path(fwr["path"]).resolve()
    if not path.is_relative_to(work) or sha256(path) != fwr["sha256"]:
        raise SystemExit("FWR result verification failed")
    if manifest["static_gap_to_msp_bytes"] <= 0 or not manifest["bin_fwr_roundtrip"]:
        raise SystemExit("memory/package check failed")
    target = cache / "output" / build_id
    target.mkdir(parents=True)
    final = target / f"comMTTL-W01_{manifest['version']}-build-{manifest['build_id']}.fwr"
    shutil.copy2(path, final)
    shutil.copy2(out / "manifest.json", target / "manifest.json")
    log(f"built {final}")
    log(f"SHA-256 {sha256(final)} · static gap to MSP {manifest['static_gap_to_msp_bytes']} bytes")
    return final


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=HERE.parent / "source")
    parser.add_argument("--cache", type=Path, default=Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "mttl-w01-builder")
    args = parser.parse_args()
    env_dir = prepare(args.cache.resolve())
    build(args.source.resolve(), args.cache.resolve(), env_dir)


if __name__ == "__main__":
    sys.exit(main())
