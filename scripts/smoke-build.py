#!/usr/bin/env python3
"""The harness bootstrap's smoke build: the library core and its tests, without GUI.Forms.

    python3 scripts/smoke-build.py [--jobs N]

Run from the workspace root (Pocket Library's checkout). Configures
.build/native-<platform>/books-smoke with GAMES_BUILD_APPLICATION=OFF, which
leaves out GUI.Forms, the shell and every test that reads the library data
or runtime assets. It builds what remains with clang (and ccache when
present) and runs its tests: dogfood, paper_engine and pocketlibrary_library.
It needs only CMake, Ninja, Python and the LLVM 22.1.8 toolchain the
bootstrap restores. The harness toolchain file is used when the harness is
checked out at template_harness/; on macOS only when the LLVM runtime prefix
it needs is named (BOOKS_LLVM_RUNTIME, HARNESS_LLVM_RUNTIME or
GUI_FORMS_LLVM_RUNTIME).

Exits nonzero when the configure, the build or a test fails; the build tree
is left in place for inspection.
"""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_FILE = ROOT / "template_harness" / "harness" / "cmake" / "llvm22.cmake"
RUNTIME_VARIABLES = ("BOOKS_LLVM_RUNTIME", "HARNESS_LLVM_RUNTIME", "GUI_FORMS_LLVM_RUNTIME")


def host_platform():
    """The harness platform name of this host: windows-x64, linux-arm64, macos-arm64 and so on."""
    systems = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}
    system = systems.get(platform.system(), platform.system().lower())
    if system.startswith(("mingw", "msys", "cygwin")):
        system = "windows"
    architecture = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    return system + "-" + architecture


def toolchain_arguments(host):
    """The harness toolchain file when it is present and, on macOS, its runtime prefix is named.

    The toolchain file reads <HARNESS_ENV_PREFIX>_LLVM_RUNTIME, so the first
    named prefix is passed on as BOOKS_LLVM_RUNTIME with HARNESS_ENV_PREFIX=BOOKS.
    """
    if not TOOLCHAIN_FILE.is_file():
        return []
    if host.startswith("macos"):
        runtime = ""
        for name in RUNTIME_VARIABLES:
            runtime = os.environ.get(name, "")
            if runtime:
                break
        if not runtime:
            return []
        os.environ["HARNESS_ENV_PREFIX"] = "BOOKS"
        os.environ["BOOKS_LLVM_RUNTIME"] = runtime
    return ["-DCMAKE_TOOLCHAIN_FILE=" + TOOLCHAIN_FILE.as_posix()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()
    host = host_platform()
    build = ROOT / ".build" / ("native-" + host) / "books-smoke"
    os.environ.setdefault("CC", "clang")
    os.environ.setdefault("CXX", "clang++")
    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DGAMES_BUILD_APPLICATION=OFF"]
    configure.extend(toolchain_arguments(host))
    if shutil.which("ccache"):
        os.environ.setdefault("CCACHE_DIR", str(ROOT / ".ccache"))
        os.environ.setdefault("CCACHE_BASEDIR", str(ROOT))
        os.environ.setdefault("CCACHE_COMPILERCHECK", "content")
        configure.extend(["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"])
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(args.jobs)], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure", "--timeout", "180"], check=True)
    print("Smoke build passed: " + build.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
