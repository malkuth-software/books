#!/usr/bin/env python3
"""Restores the Windows toolchain GUI.Forms compiled its published cache with.

GUI.Forms publishes its MSYS2 CLANG64 tree (clang 22.1.8, lld, cmake, ninja, ccache,
python, nsis, git and the rest) beside its compiler cache in build-<revision>. This
downloads it for the pinned revision, checks the archive against the digest recorded
here and in its manifest, and extracts it to the manifest's producer.msys_root
(D:\\a\\_temp\\msys64 on a hosted windows-2022 runner): system header paths enter what
ccache hashes, so anywhere else builds correctly but misses the cache. It then checks
clang.exe against the manifest and writes the msys2.cmd wrapper setup-msys2 would, so
`shell: msys2 {0}` steps run in it. No step talks to the MSYS2 mirrors.

Anything unexpected is reported and leaves restored=false in GITHUB_OUTPUT; the
workflow then falls back to setup-msys2. Needs Python 3.14 (the standard library's zstd).
"""
import hashlib
import json
import os
import subprocess
import sys
import tarfile
from pathlib import Path, PurePosixPath

from package_common import TOOLKIT_REVISION

REPOSITORY = "falseywinchnet/gui_forms"
ARCHIVE = "gui-forms-toolchain-windows-x64.tar.zst"
MANIFEST = "gui-forms-toolchain-windows-x64.json"
# The toolchain in release build-<TOOLKIT_REVISION>, and the clang.exe inside it (the
# same compiler GUI.Forms' Windows cache manifest records).
ARCHIVE_SHA256 = "de97be8abaf87c943ac627e7fdb16f7688b188015bd3bbd23112498433ed2182"
COMPILER_SHA256 = "071874172edc90ea355def3e7e253a27a82646b20b3ca393962621b52f32a708"
COMPILER = "msys64/clang64/bin/clang.exe"
ROOT = "msys64"


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(name: str, directory: Path) -> Path:
    subprocess.run(["gh", "release", "download", "build-" + TOOLKIT_REVISION, "--repo", REPOSITORY,
                    "--pattern", name, "--dir", str(directory), "--clobber"], check=True)
    return directory / name


def restore(work: Path) -> Path:
    work.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(download(MANIFEST, work).read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("platform") != "windows-x64" or manifest.get("root") != ROOT:
        raise ValueError("unexpected toolchain manifest")
    if manifest.get("archive_sha256") != ARCHIVE_SHA256 or manifest.get("compiler_sha256") != COMPILER_SHA256:
        raise ValueError("the published toolchain is not the one recorded here")
    archive = download(ARCHIVE, work)
    if file_digest(archive) != ARCHIVE_SHA256:
        raise ValueError(ARCHIVE + " digest mismatch")
    msys_root = Path(manifest["producer"]["msys_root"])
    if msys_root.name != ROOT:
        raise ValueError("unexpected msys_root " + str(msys_root))
    if msys_root.exists():
        raise ValueError(str(msys_root) + " already exists")
    destination = msys_root.parent
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:zst") as payload:
        for member in payload.getmembers():
            name = PurePosixPath(member.name)
            if (not name.parts or name.is_absolute() or ".." in name.parts or name.parts[0] != ROOT
                    or not (member.isfile() or member.isdir() or member.islnk() or member.issym())):
                raise ValueError("unexpected archive member " + member.name)
        payload.extractall(destination, filter="data")
    archive.unlink()
    if file_digest(destination / COMPILER) != COMPILER_SHA256:
        raise ValueError("the extracted clang.exe is not the recorded compiler")
    # The archive leaves out MSYS2's empty working folders; tools (windres, the C++
    # library's temp_directory_path) need /tmp and the rest to exist.
    for empty in ("tmp", "var/tmp", "var/log", "var/cache", "home"):
        (msys_root / empty).mkdir(parents=True, exist_ok=True)
    return msys_root


def write_wrapper(msys_root: Path, directory: Path) -> None:
    # The same msys2.cmd setup-msys2 writes for `shell: msys2 {0}`.
    directory.mkdir(parents=True, exist_ok=True)
    lines = ["@echo off", "setlocal", "IF NOT DEFINED MSYSTEM set MSYSTEM=CLANG64",
             "IF NOT DEFINED MSYS2_PATH_TYPE set MSYS2_PATH_TYPE=inherit", "set CHERE_INVOKING=1",
             str(msys_root) + "\\usr\\bin\\bash.exe -leo pipefail %*"]
    (directory / "msys2.cmd").write_bytes("\r\n".join(lines).encode("utf-8"))


def append(variable: str, line: str) -> None:
    with Path(os.environ[variable]).open("a", encoding="utf-8") as stream:
        stream.write(line + "\n")


def main() -> None:
    temporary = Path(os.environ.get("RUNNER_TEMP", ".build"))
    try:
        msys_root = restore(temporary / "windows-toolchain")
        write_wrapper(msys_root, temporary / "setup-msys2")
    except (OSError, ValueError, KeyError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print("Windows toolchain not restored (" + str(error) + "); setup-msys2 will install one", flush=True)
        append("GITHUB_OUTPUT", "restored=false")
        return
    append("GITHUB_PATH", str(temporary / "setup-msys2"))
    append("GITHUB_OUTPUT", "restored=true")
    print("Restored GUI.Forms' Windows toolchain for " + TOOLKIT_REVISION[:7] + " at " + str(msys_root), flush=True)


if __name__ == "__main__":
    if sys.version_info < (3, 14):
        print("Windows toolchain not restored (Python 3.14 is needed for zstd); setup-msys2 will install one")
        append("GITHUB_OUTPUT", "restored=false")
    else:
        main()
