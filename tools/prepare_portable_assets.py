#!/usr/bin/env python3
"""Prepare runtime pixels and portable audio; original authoring assets remain unchanged."""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path


def prepare_fonts(source: Path, destination: Path) -> None:
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    for record in manifest["files"]:
        filename = record["file"]
        if Path(filename).name != filename:
            raise ValueError("Font manifest requires simple filenames")
        digest = hashlib.sha256((source / filename).read_bytes()).hexdigest()
        if digest != record["sha256"]:
            raise ValueError("Font or license fingerprint differs: " + filename)
    destination.mkdir(parents=True, exist_ok=True)
    for record in manifest["files"]:
        shutil.copy2(source / record["file"], destination / record["file"])
    shutil.copy2(source / "manifest.json", destination / "manifest.json")
    shutil.copy2(source / "ATTRIBUTION.md", destination / "ATTRIBUTION.md")


AUDIO_EXTENSIONS = {".m4a", ".wav", ".ogg", ".mp3", ".flac"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def asset_inventory(source: Path, extra_dirs=()) -> dict:
    """Inventory source bytes, including enabled folder modules. No timestamps.

    Module audio filenames retain their producer prefix and merge into audio/;
    all other module assets keep their relative paths below the module id.
    """
    from game_catalog import discover
    source = source.resolve()
    modules = discover(source.parent, extra_dirs)
    roots = [("root", source, "")]
    manifests = {}
    for module in modules:
        manifest = module["manifest_path"]
        manifests[module["id"]] = sha256(manifest)
        relative = module.get("assets", "assets")
        if relative is False or relative is None:
            continue
        asset_root = (module["directory"] / relative).resolve()
        if not asset_root.is_relative_to(module["directory"].resolve()):
            raise ValueError("Module assets escape its directory: " + module["id"])
        if asset_root.is_dir():
            prefix = module.get("audio_prefix", module["id"] + "_")
            if not isinstance(prefix, str) or not re.fullmatch(r"[a-z][a-z0-9_]*_", prefix):
                raise ValueError("Module audio_prefix must be a lowercase identifier ending in _: " + module["id"])
            roots.append((module["id"], asset_root, prefix))
        elif "assets" in module:
            raise ValueError("Missing module assets: " + str(asset_root))
    audio, resources, producers, loops = {}, {}, {}, {}
    for owner, root, prefix in roots:
        local_audio = {}
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if not path.resolve().is_relative_to(root):
                raise ValueError("Asset symlink escapes source: " + str(path))
            relative = path.relative_to(root)
            key = owner + "/" + relative.as_posix()
            if relative.parts[0] == "audio" and path.suffix.lower() in AUDIO_EXTENSIONS:
                stem = path.stem
                if not re.fullmatch(r"[A-Za-z0-9_.-]+", stem):
                    raise ValueError("Audio stem must be a portable filename: " + stem)
                if prefix and not stem.startswith(prefix):
                    raise ValueError("Module audio requires prefix " + prefix + ": " + str(path))
                if stem.casefold() in audio:
                    raise ValueError("Audio filename collision: " + stem)
                record = {"path": path, "source": key, "stem": stem, "source_sha256": sha256(path)}
                audio[stem.casefold()] = record
                local_audio[stem] = record
            elif owner != "root":
                output = owner + "/" + relative.as_posix()
                if (source / output).exists() or output.casefold() in resources:
                    raise ValueError("Module asset collision: " + output)
                resources[output.casefold()] = {"path": path, "file": output, "source_sha256": sha256(path)}
        audio_root = root / "audio"
        for path in sorted(audio_root.rglob("*.json")) if audio_root.is_dir() else []:
            document = json.loads(path.read_text(encoding="utf-8"))
            producer_key = owner + "/" + path.relative_to(root).as_posix()
            producers[producer_key] = {"sha256": sha256(path), "metadata": document}
            if not isinstance(document, dict):
                continue
            for entry in document.get("music", []):
                if not isinstance(entry, dict):
                    raise ValueError("Invalid music entry: " + producer_key)
                start, end = entry.get("loop_start_sample", 0), entry.get("loop_end_sample_exclusive")
                if type(start) is not int or type(end) is not int or not 0 <= start < end <= 28800000:
                    raise ValueError("Missing or invalid music loop bounds: " + producer_key)
                if entry.get("sample_rate", document.get("sample_rate", 48000)) != 48000:
                    raise ValueError("Loop metadata must use 48000 Hz sample units: " + producer_key)
                identifier = entry.get("id", "")
                explicit = entry.get("runtime", [])
                if isinstance(explicit, str):
                    explicit = [explicit]
                candidates = [Path(name).stem for name in explicit if isinstance(name, str)]
                filename = entry.get("file", "")
                if isinstance(filename, str):
                    candidates.append(Path(filename).stem)
                candidates.extend([identifier, "music_" + identifier + "_loop"])
                matches = {name for name in candidates if name in local_audio}
                if len(matches) != 1:
                    raise ValueError("Missing or ambiguous music source: " + producer_key + ": " + identifier)
                stem = matches.pop()
                bounds = {"start": start, "end": end}
                if stem in loops and loops[stem] != bounds:
                    raise ValueError("Conflicting producer loop bounds: " + stem)
                loops[stem] = bounds
        for stem in local_audio:
            if ("music" in stem.lower() or "loop" in stem.lower()) and stem not in loops:
                raise ValueError("Missing producer loop metadata: " + stem)
    # A pinned asset (tools/pinned_assets.py) is a resource whether or not it has been
    # fetched yet: its pin says what its bytes are, so the verification of a runtime
    # restored from a cache needs no download.
    import pinned_assets
    enabled = {owner for owner, _, _ in roots}
    for owner, pin, path in pinned_assets.pins(source.parent):
        output = owner + "/" + pin["path"]
        if owner in enabled and output.casefold() not in resources:
            resources[output.casefold()] = {"path": path, "file": output, "source_sha256": pin["sha256"]}
    return {"audio": sorted(audio.values(), key=lambda value: value["source"]),
            "resources": sorted(resources.values(), key=lambda value: value["file"]),
            "producer_manifests": producers, "module_manifests": manifests, "loops": loops}


def loop_bounds(source: Path) -> dict[str, int]:
    return {stem: bounds["end"] - bounds["start"] for stem, bounds in asset_inventory(source)["loops"].items()}


def audio_verification(manifest: dict) -> str:
    """Small native-test contract, itself verified against full provenance JSON."""
    lines = [str(len(manifest["files"]))]
    for record in sorted(manifest["files"], key=lambda record: record["file"]):
        if "producer_loop" in record:
            lines.append(Path(record["file"]).stem + "\t" + str(record["frames"]))
    return "\n".join(lines) + "\n"


def prepare_audio(ffmpeg: str, source: Path, destination: Path, frames: int, codec: str, start: int = 0) -> dict:
    temporary = destination.with_suffix(".f32")
    subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                    "-vn", "-ar", "48000", "-ac", "2", "-f", "f32le", str(temporary)], check=True)
    size = temporary.stat().st_size
    if size == 0 or size % 8 != 0 or size > 230400000:
        raise ValueError("Invalid or excessive decoded PCM length: " + source.name)
    available = size // 8
    if frames == 0:
        frames = available
    if start + frames > available:
        raise ValueError("Decoded PCM is shorter than producer loop: " + source.name)
    size = frames * 8
    # A minimal RIFF IEEE-float file avoids platform decoder/extended-header differences.
    pcm_path = destination if codec == "pcm" else destination.with_suffix(".trimmed.wav")
    with pcm_path.open("wb") as output, temporary.open("rb") as pcm:
        output.write(b"RIFF" + struct.pack("<I", size + 36) + b"WAVEfmt ")
        output.write(struct.pack("<IHHIIHH", 16, 3, 2, 48000, 384000, 8, 32))
        output.write(b"data" + struct.pack("<I", size))
        pcm.seek(start * 8)
        remaining = size
        while remaining > 0:
            data = pcm.read(min(remaining, 65536))
            if not data:
                raise ValueError("PCM truncated during packaging")
            output.write(data)
            remaining -= len(data)
    temporary.unlink()
    if codec == "vorbis":
        subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(pcm_path),
                        "-c:a", "libvorbis", "-q:a", "6", str(destination)], check=True)
        pcm_path.unlink()
    record = {"source": source.name, "file": destination.name, "frames": frames,
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--extra-game-dir", type=Path, action="append", default=[])
    parser.add_argument("--audio-format", choices=("pcm", "vorbis"), default="vorbis")
    options = parser.parse_args()
    source = options.source.resolve()
    output = options.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError("Runtime output must be separate from source assets")
    extra_dirs = options.extra_game_dir + [Path(value) for value in os.environ.get("GAMES_EXTRA_GAME_DIRS", "").split(";") if value]
    # Assets too large for Git (books/<id>/pinned_assets.json) are fetched and checked
    # first, so they are gathered like any other.
    import pinned_assets
    pinned_assets.fetch(source.parent)
    inventory = asset_inventory(source, extra_dirs)
    output.mkdir(parents=True, exist_ok=True)
    prepare_fonts(source / "fonts", output / "fonts")
    for path in source.iterdir():
        if path.name in ("audio", "fonts"):
            continue
        target = output / path.name
        if path.is_dir():
            shutil.copytree(path, target, dirs_exist_ok=True)
        else:
            shutil.copy2(path, target)
    audio = output / "audio"
    audio.mkdir(exist_ok=True)
    extension = ".wav" if options.audio_format == "pcm" else ".ogg"
    unexpected = ".ogg" if extension == ".wav" else ".wav"
    if any(audio.glob("*" + unexpected)):
        raise ValueError("Use a separate output directory for each runtime audio format")
    # Incremental: a file prepared before (an output directory restored from a cache, or
    # left by the last run) is kept when its source bytes, loop bounds and format are
    # unchanged and its own bytes are intact. Only new or changed sources are encoded,
    # so adding a game costs its own sounds, not the whole collection's.
    previous: dict[str, dict] = {}
    previous_resources: set[str] = set()
    audio_format = "IEEE float32 LE" if options.audio_format == "pcm" else "Ogg Vorbis quality 6"
    previous_manifest = audio / "portable_manifest.json"
    if previous_manifest.is_file():
        try:
            old = json.loads(previous_manifest.read_text(encoding="utf-8"))
            if old.get("format") == audio_format:
                previous = {record["source"]: record for record in old.get("files", [])}
            previous_resources = {record["file"] for record in old.get("module_resources", [])}
        except (ValueError, KeyError, TypeError):
            previous = {}
    records: list[dict] = []
    reused = 0
    for entry in inventory["audio"]:
        bounds = inventory["loops"].get(entry["stem"], {"start": 0, "end": 0})
        destination = audio / (entry["stem"] + extension)
        looped = entry["stem"] in inventory["loops"]
        old = previous.get(entry["source"])
        if (old is not None and old.get("source_sha256") == entry["source_sha256"]
                and old.get("file") == destination.name and old.get("producer_loop") == (bounds if looped else None)
                and destination.is_file() and hashlib.sha256(destination.read_bytes()).hexdigest() == old.get("sha256")):
            records.append(old)
            reused += 1
            continue
        record = prepare_audio(options.ffmpeg, entry["path"], destination,
                               bounds["end"] - bounds["start"], options.audio_format, bounds["start"])
        record["source"] = entry["source"]
        if looped:
            record["producer_loop"] = bounds
        records.append(record)
    # Whatever no source produced this time (a removed or renamed sound) is stale.
    kept = {record["file"] for record in records} | {"portable_manifest.json", "verification.tsv"}
    for path in audio.iterdir():
        if path.is_file() and path.name not in kept:
            path.unlink()
    # A game resource the last run copied that no source provides now (a removed or
    # renamed file) is stale too.
    for name in previous_resources - {entry["file"] for entry in inventory["resources"]}:
        stale = output / name
        if stale.is_file() and output.resolve() in stale.resolve().parents:
            stale.unlink()
    resources = []
    for entry in inventory["resources"]:
        target = output / entry["file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry["path"], target)
        # Readable by everyone, whatever the source's own mode: the packages install as
        # root, and the suite runs as the person playing.
        target.chmod(0o644)
        resources.append({key: entry[key] for key in ("file", "source_sha256")})
    # What the last run copied from the shared assets folder that no source provides now (a
    # removed file) is stale as well: a runtime restored from a cache keeps everything it held.
    produced = {"audio", "fonts"} | {Path(entry["file"]).parts[0] for entry in inventory["resources"]}
    for path in list(output.iterdir()):
        if path.name in produced:
            continue
        origin = source / path.name
        if not origin.exists() or origin.is_dir() != path.is_dir():
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
        elif path.is_dir():
            for inner in sorted(path.rglob("*"), reverse=True):
                relative = inner.relative_to(output)
                if inner.is_file() and not (source / relative).is_file():
                    inner.unlink()
                elif inner.is_dir() and not any(inner.iterdir()):
                    inner.rmdir()
    manifest = {"schema_version": 2, "sample_rate": 48000, "channels": 2, "format": audio_format,
                "files": records, "module_resources": resources,
                "producer_manifests": inventory["producer_manifests"],
                "module_manifests": inventory["module_manifests"]}
    (audio / "portable_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (audio / "verification.tsv").write_text(audio_verification(manifest), encoding="utf-8")
    print("Prepared", len(records), audio_format, "assets at", output,
          f"({reused} unchanged and reused, {len(records) - reused} encoded)")


if __name__ == "__main__":
    main()
