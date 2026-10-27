"""Exercise the per-user installer in the disposable Windows CI account."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("installer", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pocketlibrary-installer-test-") as scratch:
        # NSIS takes /D= and _?= unquoted, and Python quotes an argument with a space in it,
        # so the test folder has none (the installer's own default, "Pocket Library", does).
        installed = Path(scratch) / "PocketLibrary"
        native_directory = str(installed).replace("/", "\\")
        subprocess.run([str(args.installer.resolve()), "/S", "/D=" + native_directory], check=True, timeout=90)
        if not (installed / "pocketlibrary.exe").is_file():
            raise RuntimeError("Installer did not create the application")
        subprocess.run([sys.executable, str(Path(__file__).with_name("smoke_package.py")),
                        str(installed / "pocketlibrary.exe")], check=True, timeout=90)
        # NSIS's _?= option runs the uninstaller in place and waits for completion.
        subprocess.run([str(installed / "Uninstall.exe"), "/S", "_?=" + native_directory], check=True, timeout=90)
        if (installed / "pocketlibrary.exe").exists() or (installed / "assets").exists():
            raise RuntimeError("Uninstaller left application payload behind")


if __name__ == "__main__":
    main()
