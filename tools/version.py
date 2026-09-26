# SPDX-License-Identifier: GPL-3.0-only
"""Resolve firmware identity from the checked-out source."""

import subprocess

from bundle import valid_version


def identity(root, tag=None):
    def git(*args):
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True
        ).strip()

    revision = git("rev-parse", "--short=8", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=normal"))
    tags = git("tag", "--points-at", "HEAD").splitlines()
    if tag is not None:
        if not tag.startswith("v") or not valid_version(tag[1:]):
            raise ValueError("Release tags must be v followed by SemVer")
        if dirty or tag not in tags:
            raise ValueError("A release requires a clean checkout at the requested tag")
    elif not dirty:
        releases = [t for t in tags if t.startswith("v") and valid_version(t[1:])]
        if len(releases) > 1:
            raise ValueError("Multiple release tags at HEAD; select one with --tag")
        tag = next(iter(releases), None)
    revision += "-dirty" if dirty else ""
    return {
        "version": tag[1:] if tag else "0.0.0-dev+" + revision,
        "revision": revision,
    }
