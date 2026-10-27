"""Run the packaged native host with isolated saves and a bounded lifetime."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    environment = os.environ.copy()
    for variable in ("GAMES_ASSET_DIR", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        environment.pop(variable, None)
    if os.name == "nt":
        environment["PATH"] = environment.get("SystemRoot", "C:\\Windows") + "\\System32"
    with tempfile.TemporaryDirectory(prefix="pocketlibrary-package-smoke-") as state:
        environment["GAMES_STATE_DIR"] = state
        result = subprocess.run([str(args.executable.resolve()), "--smoke-test"],
                                env=environment, capture_output=True, text=True, timeout=60, check=True)
        print(result.stdout)
        print(result.stderr)
        if "Native Pocket Library window smoke passed" not in result.stdout:
            raise RuntimeError("Native host did not finish its scheduled close")
        receipts = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        receipts = [receipt for receipt in receipts if "window" in receipt]
        if not receipts or receipts[0]["window"]["frames_presented"] < 1:
            raise RuntimeError("Native host did not present the collection")
        renderer = receipts[0]["window"]["renderer_name"]
        if "incomplete" in renderer or "fallback" == renderer:
            raise RuntimeError("Native host did not load its required bundled fonts")


if __name__ == "__main__":
    main()
