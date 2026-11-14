#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch the pinned InfoNES core and apply the RepriseOS integration patch."""

import hashlib
import io
import subprocess
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
COMMIT = "fe3295c0a86bf5bbecc22aeab3b3af11f6f47908"
URL = f"https://codeload.github.com/jay-kumogata/InfoNES/tar.gz/{COMMIT}"
SHA256 = "5af1bac2584c70e27e534734c4e3581e234c00fb9c0a0947c2381ea93296184d"


def setup():
    destination = ROOT / "vendor/infones"
    patch = ROOT / "patches/osos.patch"
    identity = SHA256 + ":" + hashlib.sha256(patch.read_bytes()).hexdigest()
    stamp = destination / ".source-sha256"
    if stamp.exists() and stamp.read_text().strip() == identity:
        return
    if destination.exists():
        raise RuntimeError(f"Unrecognized or outdated source tree: {destination}")
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("InfoNES source checksum mismatch")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts[1:]
            if not member.isfile() or not parts:
                continue
            if ".." in parts or Path(*parts).is_absolute():
                raise ValueError("Unsafe source archive path")
            if parts[0] not in {"src", "LICENSE", "README.md"}:
                continue
            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(
                archive.extractfile(member).read().replace(b"\r\n", b"\n")
            )
    subprocess.run(["patch", "-p1", "-i", str(patch)], cwd=destination, check=True)
    stamp.write_text(identity + "\n")
    print(f"InfoNES {COMMIT} ready")


if __name__ == "__main__":
    setup()
