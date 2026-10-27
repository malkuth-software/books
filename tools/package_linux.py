"""Package the native Linux application for Ubuntu 24.04 or compatible systems."""
import argparse
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
from package_common import ROOT, copy_resources, project_version, write_manifest


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
    # GUI.Forms' shared libraries (Application, Threading) the bundle needs, followed
    # through each one's own imports.
    pending = [bundle / "pocketlibrary"]
    while pending:
        binary = pending.pop()
        imports = subprocess.check_output(["patchelf", "--print-needed", str(binary)], text=True).splitlines()
        for name in imports:
            if name.startswith("libgui_forms_") and not (bundle / name).exists():
                shutil.copy2(args.build / "toolkit" / name, bundle / name)
                pending.append(bundle / name)
    if not list(bundle.glob("libgui_forms_application.so*")):
        raise RuntimeError("Expected the GUI.Forms application library import")
    for binary in [bundle / "pocketlibrary"] + list(bundle.glob("*.so*")):
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(binary)], check=True)
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=True)
        dependencies = subprocess.check_output(["ldd", str(binary)], text=True)
        if "not found" in dependencies:
            raise RuntimeError(dependencies)
    copy_resources(args.build, args.toolkit, bundle)
    arch = "arm64" if platform.machine() == "aarch64" else "amd64"
    write_manifest(bundle, "Linux", arch)
    stem = "pocketlibrary-" + project_version() + "-linux-" + arch
    with tarfile.open(output / (stem + ".tar.gz"), "w:gz") as archive:
        archive.add(bundle, arcname="Pocket Library")
    root = output / "deb-root"
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
                        "Maintainer: Rainstar <noreply@rainstar.invalid>\n\n"
                        "Package: rainstar-pocketlibrary\nArchitecture: any\nDescription: Pocket Library, a native reading room of books\n", encoding="utf-8")
    binaries = [installed / "pocketlibrary"] + list(installed.glob("*.so*"))
    dependencies = subprocess.check_output(
        ["dpkg-shlibdeps", "-O", "-l" + str(installed), "--ignore-missing-info"] +
        ["-e" + str(binary) for binary in binaries], cwd=output, text=True).strip().removeprefix("shlibs:Depends=")
    control.write_text("Package: rainstar-pocketlibrary\nVersion: " + project_version() + "\nArchitecture: " + arch +
        "\nMaintainer: Rainstar <noreply@rainstar.invalid>\nSection: text\nPriority: optional\nDepends: " +
        dependencies + ", libasound2t64\nDescription: A reading room of books, each set as a printed book\n", encoding="utf-8")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(root), str(output / (stem + ".deb"))], check=True)


if __name__ == "__main__":
    main()
