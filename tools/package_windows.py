"""Stage the PE import closure, portable archive and optional per-user installer."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from package_common import ROOT, copy_resources, project_version, write_manifest

SYSTEM = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll",
          "ole32.dll", "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll",
          "usp10.dll", "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll",
          "dwmapi.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "bcrypt.dll",
          "secur32.dll", "rpcrt4.dll", "setupapi.dll", "winspool.drv", "winmm.dll",
          "crypt32.dll", "normaliz.dll", "uxtheme.dll", "msimg32.dll", "shcore.dll",
          "dbghelp.dll", "psapi.dll", "userenv.dll", "iphlpapi.dll",
          "uiautomationcore.dll"}


def find_makensis():
    """The official NSIS when the machine has it (GitHub's Windows image ships 3.10),
    otherwise the one on PATH. MSYS2's NSIS 3.13 pairs 32-bit installer stubs with
    64-bit plugins, so makensis rejects every plugin and MUI's pages cannot build."""
    for root in (os.environ.get("NSIS_HOME", ""), os.environ.get("ProgramFiles(x86)", ""), os.environ.get("ProgramFiles", ""),
                 "C:/Program Files (x86)", "C:/Program Files"):
        if not root:
            continue
        for candidate in (Path(root) / "makensis.exe", Path(root) / "NSIS" / "makensis.exe"):
            if candidate.is_file():
                return str(candidate)
    return "makensis"


def make_installer(bundle, output):
    version = project_version()
    installer = output / ("pocketlibrary-" + version + "-windows-x64-setup.exe")
    script = output / "pocketlibrary-installer.nsi"
    # The vendor people see is Malkuth Software (owner, 2026-10-27): the
    # publisher, the Start menu folder and the installer's version resource.
    # The machine identities stay those of earlier releases, so an upgrade
    # replaces the installed copy in place: the default folder under Rainstar\
    # and the RainstarPocketLibrary uninstall key (on the other hosts, the
    # package name, the bundle identifier and the per-user save folders).
    # InstallLocation lets later upgrades find a folder the person chose.
    uninstall_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RainstarPocketLibrary"
    lines = ['Unicode True', '!include "MUI2.nsh"', 'Name "Pocket Library"',
             'OutFile "' + str(installer).replace("/", "\\") + '"', 'InstallDir "$LOCALAPPDATA\\Rainstar\\Pocket Library"',
             'InstallDirRegKey HKCU "' + uninstall_key + '" "InstallLocation"',
             'BrandingText "Malkuth Software"', 'VIProductVersion "' + version + '.0"',
             'VIAddVersionKey "ProductName" "Pocket Library"', 'VIAddVersionKey "CompanyName" "Malkuth Software"',
             'VIAddVersionKey "FileDescription" "Pocket Library installer"',
             'VIAddVersionKey "FileVersion" "' + version + '"', 'VIAddVersionKey "ProductVersion" "' + version + '"',
             'RequestExecutionLevel user', '!insertmacro MUI_PAGE_WELCOME',
             '!insertmacro MUI_PAGE_DIRECTORY', '!insertmacro MUI_PAGE_INSTFILES',
             '!insertmacro MUI_PAGE_FINISH', '!insertmacro MUI_UNPAGE_CONFIRM',
             '!insertmacro MUI_UNPAGE_INSTFILES', '!insertmacro MUI_LANGUAGE "English"',
             'Section "Pocket Library"', 'SetOutPath "$INSTDIR"', 'File /r "' + str(bundle / "*").replace("/", "\\") + '"',
             # An earlier release's Start menu folder gives way to the vendor's.
             'Delete "$SMPROGRAMS\\Rainstar\\Pocket Library.lnk"', 'RMDir "$SMPROGRAMS\\Rainstar"',
             'CreateDirectory "$SMPROGRAMS\\Malkuth Software"',
             'CreateShortcut "$SMPROGRAMS\\Malkuth Software\\Pocket Library.lnk" "$INSTDIR\\pocketlibrary.exe"',
             'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
             'WriteRegStr HKCU "' + uninstall_key + '" "DisplayName" "Pocket Library"',
             'WriteRegStr HKCU "' + uninstall_key + '" "UninstallString" \'$\\"$INSTDIR\\Uninstall.exe$\\"\'',
             'WriteRegStr HKCU "' + uninstall_key + '" "Publisher" "Malkuth Software"',
             'WriteRegStr HKCU "' + uninstall_key + '" "DisplayVersion" "' + version + '"',
             'WriteRegStr HKCU "' + uninstall_key + '" "InstallLocation" "$INSTDIR"',
             'SectionEnd', 'Section "Uninstall"']
    directories = []
    for path in sorted(bundle.rglob("*")):
        relative = str(path.relative_to(bundle)).replace("/", "\\")
        if path.is_file():
            lines.append('Delete "$INSTDIR\\' + relative + '"')
        elif path.is_dir():
            directories.append(relative)
    # Remove only installed files and empty directories. Never recursively
    # remove a user-selected directory or any per-user save directory.
    for relative in sorted(directories, key=len, reverse=True):
        lines.append('RMDir "$INSTDIR\\' + relative + '"')
    lines += ['Delete "$INSTDIR\\Uninstall.exe"', 'RMDir "$INSTDIR"',
              'Delete "$SMPROGRAMS\\Malkuth Software\\Pocket Library.lnk"', 'RMDir "$SMPROGRAMS\\Malkuth Software"',
              'Delete "$SMPROGRAMS\\Rainstar\\Pocket Library.lnk"', 'RMDir "$SMPROGRAMS\\Rainstar"',
              'DeleteRegKey HKCU "' + uninstall_key + '"', 'SectionEnd']
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    makensis = find_makensis()
    print("Building the installer with", makensis, flush=True)
    subprocess.run([makensis, str(script)], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--installer", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    bundle = output / "Pocket Library"
    bundle.mkdir(parents=True, exist_ok=False)
    executable = bundle / "pocketlibrary.exe"
    shutil.copy2(args.build / "pocketlibrary.exe", executable)
    candidates = {}
    for directory in (args.build, args.build / "toolkit", args.runtime_dir):
        for path in directory.glob("*.dll"):
            candidates.setdefault(path.name.lower(), path)
    pending = [executable]
    inspected = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in inspected:
            continue
        inspected.add(binary.name.lower())
        imports = subprocess.check_output(["objdump", "-p", str(binary)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", imports):
            key = name.lower()
            if key in SYSTEM or key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if key not in candidates:
                raise RuntimeError("Missing runtime import: " + name)
            target = bundle / name
            if not target.exists():
                shutil.copy2(candidates[key], target)
            pending.append(target)
    for binary in bundle.iterdir():
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=True)
    copy_resources(args.build, args.toolkit, bundle)
    runtime_notices = bundle / "licenses/runtime"
    runtime_notices.mkdir()
    bundled = {path.name.lower() for path in bundle.iterdir()}
    for notice in (ROOT / "packaging/licenses").glob("*.txt"):
        # GCC's runtime notices only when its runtime is shipped (a MINGW64 build)
        if notice.name.startswith("GCC-") and not bundled & {"libstdc++-6.dll", "libgcc_s_seh-1.dll"}:
            continue
        shutil.copy2(notice, runtime_notices / notice.name)
    # LLVM's libc++ and libunwind (a CLANG64 build)
    if bundled & {"libc++.dll", "libunwind.dll"}:
        shutil.copytree(ROOT / "packaging/licenses/llvm", runtime_notices / "LLVM")
    write_manifest(bundle, "Windows", "x64")
    archive = output / ("pocketlibrary-" + project_version() + "-windows-x64.zip")
    # A source SDK unpacks with zero timestamps; ZIP cannot hold dates before
    # 1980, so those entries are clamped to 1980-01-01 instead of refused.
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9,
                         strict_timestamps=False) as compressed:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                compressed.write(path, "Pocket Library/" + path.relative_to(bundle).as_posix())
    if args.installer:
        make_installer(bundle, output)
    print(archive)


if __name__ == "__main__":
    main()
