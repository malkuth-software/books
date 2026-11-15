"""Package the native Linux application for Ubuntu 24.04 or compatible systems."""
import argparse
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
from package_common import ROOT, copy_resources, project_version, write_manifest

# A shipped program is one module with no native library dependency beyond the system's own
# (owner, 2026-11-15): libc++, libc++abi and LLVM's libunwind are linked statically
# (gui_forms/cmake/llvm22.cmake, GUI.Forms claude/cp33-libcxx-static 39d2ea6b0), and GUI.Forms
# is linked into the executable (GUI.Forms claude/one-module). The package refuses a binary
# that names any of these, or any library ldd resolves outside the system's library folders.
REFUSED_LIBRARY = re.compile(r"(?<![\w+.-])(lib(?:c\+\+abi|c\+\+|unwind)\.so\.\d+|libgui_forms_\S*?\.so(?:\.\d+)*)(?![\w+.-])")
# Where the system's own libraries live (glibc and musl, both layouts); /usr/lib/llvm-* is not one.
SYSTEM_LIBRARY_FOLDERS = ("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/")
# One ldd line: "\tname => /path (0x...)", "\tname => not found", "\tname (0x...)" (vdso) or
# "\t/lib64/ld-linux... (0x...)" (the loader).
LDD_LINE = re.compile(r"^\s*(\S+)(?:\s+=>\s+(not found|\S+))?(?:\s+\(0x[0-9a-fA-F]+\))?\s*$")


def foreign_libraries(readelf_output, ldd_output):
    """The libraries a binary needs that are not the system's, in order, each once.

    readelf -d's NEEDED entries and ldd's lines are both read: a refused name (the LLVM C++
    runtime or a GUI.Forms library) fails wherever it appears; an ldd line that resolves
    nowhere, or outside SYSTEM_LIBRARY_FOLDERS, fails too. linux-vdso and the dynamic loader
    are the system's.
    """
    found = []
    for match in REFUSED_LIBRARY.finditer(readelf_output + "\n" + ldd_output):
        if match.group(1) not in found:
            found.append(match.group(1))
    for line in ldd_output.splitlines():
        match = LDD_LINE.match(line)
        if match is None or not line.strip():
            continue
        name = match.group(1)
        target = match.group(2)
        if name.startswith("linux-vdso") or name.startswith("linux-gate"):
            continue
        where = target if target is not None else name
        system = where.startswith(SYSTEM_LIBRARY_FOLDERS) and not where.startswith("/usr/lib/llvm-")
        if (where == "not found" or not system) and name not in found:
            found.append(name)
    return found


def require_one_module(binary):
    """Fails unless `binary` needs only the system's libraries (see foreign_libraries)."""
    readelf_output = subprocess.check_output(["readelf", "-d", str(binary)], text=True)
    ldd_output = subprocess.check_output(["ldd", str(binary)], text=True)
    found = foreign_libraries(readelf_output, ldd_output)
    if found:
        raise RuntimeError(str(binary) + " needs libraries that are not the system's: " + ", ".join(found)
                           + "\n" + ldd_output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    output = args.output.resolve()
    bundle = output / "Pocket Library"
    bundle.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.build / "pocketlibrary", bundle / "pocketlibrary")
    # One module: GUI.Forms and the LLVM C++ runtime are linked into the program, so the bundle
    # holds no shared library and the program needs only the system's.
    subprocess.run(["strip", "--strip-unneeded", str(bundle / "pocketlibrary")], check=True)
    require_one_module(bundle / "pocketlibrary")
    copy_resources(args.build, args.toolkit, bundle)
    # The statically linked LLVM runtime's licences, as GUI.Forms' own Linux packages carry them.
    llvm_notices = args.toolkit / "packaging/licenses/llvm"
    for name, notice in (("LLVM-libcxx.txt", "libcxx-LICENSE.TXT"), ("LLVM-libcxxabi.txt", "libcxxabi-LICENSE.TXT"),
                         ("LLVM-libunwind.txt", "libunwind-LICENSE.TXT")):
        shutil.copy2(llvm_notices / notice, bundle / "licenses" / name)
    arch = "arm64" if platform.machine() == "aarch64" else "amd64"
    write_manifest(bundle, "Linux", arch)
    stem = "pocketlibrary-" + project_version() + "-linux-" + arch
    with tarfile.open(output / (stem + ".tar.gz"), "w:gz") as archive:
        archive.add(bundle, arcname="Pocket Library")
    root = output / "deb-root"
    # The maintainer people see is Malkuth Software (owner, 2026-10-27). The
    # package name, its /usr/lib folder and the launcher keep earlier releases'
    # rainstar-pocketlibrary, so apt upgrades the installed package in place
    # instead of installing a second, conflicting one.
    maintainer = "Malkuth Software <maintainer@malkuthsoft.com>"
    installed = root / "usr/lib/rainstar-pocketlibrary"
    shutil.copytree(bundle, installed)
    launcher = root / "usr/bin/rainstar-pocketlibrary"
    launcher.parent.mkdir(parents=True)
    launcher.write_text('#!/bin/sh\nexec /usr/lib/rainstar-pocketlibrary/pocketlibrary "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    desktop = root / "usr/share/applications/rainstar-pocketlibrary.desktop"
    desktop.parent.mkdir(parents=True)
    desktop.write_text("[Desktop Entry]\nType=Application\nName=Pocket Library\nExec=rainstar-pocketlibrary\n"
                       "Icon=rainstar-pocketlibrary\nCategories=Education;Literature;\nTerminal=false\n", encoding="utf-8")
    icon = root / "usr/share/icons/hicolor/256x256/apps/rainstar-pocketlibrary.png"
    icon.parent.mkdir(parents=True)
    shutil.copy2(ROOT / "assets/PocketLibrary.png", icon)
    control = root / "DEBIAN/control"
    control.parent.mkdir()
    # dpkg-shlibdeps uses a Debian source metadata directory even for -O.
    metadata = output / "debian/control"
    metadata.parent.mkdir()
    metadata.write_text("Source: rainstar-pocketlibrary\nSection: text\nPriority: optional\n"
                        "Maintainer: " + maintainer + "\n\n"
                        "Package: rainstar-pocketlibrary\nArchitecture: any\nDescription: Pocket Library, a native reading room of books\n", encoding="utf-8")
    binaries = [installed / "pocketlibrary"] + list(installed.glob("*.so*"))
    dependencies = subprocess.check_output(
        ["dpkg-shlibdeps", "-O", "-l" + str(installed), "--ignore-missing-info"] +
        ["-e" + str(binary) for binary in binaries], cwd=output, text=True).strip().removeprefix("shlibs:Depends=")
    control.write_text("Package: rainstar-pocketlibrary\nVersion: " + project_version() + "\nArchitecture: " + arch +
        "\nMaintainer: " + maintainer + "\nHomepage: https://github.com/malkuth-software/books"
        "\nSection: text\nPriority: optional\nDepends: " +
        dependencies + ", libasound2t64\nDescription: A reading room of books, each set as a printed book\n", encoding="utf-8")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(root), str(output / (stem + ".deb"))], check=True)


if __name__ == "__main__":
    main()
