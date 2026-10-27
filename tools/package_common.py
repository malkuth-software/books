"""Shared app resources and notices; this does not install a GUI.Forms SDK."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOLKIT_REVISION = "e077f9d5a5117312e65e8c3b741c4edd24013d56"


def missing_prepared_files(assets):
    """Every sound and library resource the prepared manifest lists, absent from `assets`."""
    listed = []
    audio = assets / "audio/portable_manifest.json"
    if audio.is_file():
        manifest = json.loads(audio.read_text(encoding="utf-8"))
        listed += ["audio/" + record["file"] for record in manifest.get("files", [])]
        listed += [record["file"] for record in manifest.get("module_resources", [])]
    return [name for name in listed if not (assets / name).is_file()]


def project_version():
    from publication_version import baseline, validate
    version = os.environ.get("GAMES_RELEASE_VERSION") or baseline(ROOT)
    validate(version)
    return version


def copy_resources(build, toolkit, destination):
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    compiled_version = re.search(r"^CMAKE_PROJECT_VERSION:STATIC=(.+)$", cache, re.MULTILINE)
    if not compiled_version or compiled_version.group(1) != project_version():
        raise RuntimeError("Packaging version differs from the configured native application")
    destination.mkdir(parents=True, exist_ok=True)
    assets = build / "assets"
    if not assets.is_dir():
        assets = build / "pocketlibrary.app/Contents/Resources/assets"
    shutil.copytree(assets, destination / "assets", dirs_exist_ok=True)
    # A package that lost a prepared file would only show it when the game opens.
    missing = missing_prepared_files(destination / "assets")
    if missing:
        raise RuntimeError("Packaged assets are missing prepared files: " + ", ".join(missing[:10]))
    shutil.copytree(toolkit / "assets/fonts", destination / "fonts", dirs_exist_ok=True)
    notices = destination / "licenses"
    notices.mkdir(exist_ok=True)
    # Downloaded dependency sources: the build's _deps, or wherever the configure
    # placed them (CI keeps them in a cached FETCHCONTENT_BASE_DIR).
    base = re.search(r"^FETCHCONTENT_BASE_DIR:PATH=(.+)$", cache, re.MULTILINE)
    fetched = Path(base.group(1)) if base else build / "_deps"
    entries = {
        "GUIForms.txt": toolkit / "LICENSE",
        "Unicode.txt": toolkit / "third_party/unicode/LICENSE.txt",
        "SheenBidi.txt": toolkit / "third_party/sheenbidi/LICENSE",
        "HarfBuzz.txt": toolkit / "third_party/harfbuzz/COPYING",
        "FreeType.txt": toolkit / "third_party/freetype/docs/FTL.TXT",
        "libunibreak.txt": toolkit / "third_party/libunibreak/LICENCE",
        "miniaudio.txt": fetched / "gui_forms_miniaudio-src/LICENSE",
    }
    for name, source in entries.items():
        shutil.copy2(source, notices / name)
    skia = toolkit / "third_party/skia"
    if skia.is_dir():
        for name, source in {
            "Skia.txt": skia / "LICENSE",
            "libpng.txt": skia / "third_party/externals/libpng/LICENSE",
            "zlib.txt": skia / "third_party/externals/zlib/LICENSE",
            "Skia-FreeType.txt": skia / "third_party/externals/freetype/docs/FTL.TXT",
        }.items():
            shutil.copy2(source, notices / name)
    # Ogg Vorbis is decoded by GUI.Forms' stx_vorbis with its embedded BFFT, and
    # threads run on GUI.Forms' copy of threadpool_atomic_fast.
    for name, source in {
        "stx_vorbis.txt": toolkit / "stx_vorbis/LICENSE",
        "BFFT.txt": toolkit / "stx_vorbis/third_party/bfft/LICENSE",
        "BFFT-provenance.json": toolkit / "stx_vorbis/third_party/bfft/provenance.json",
        "threadpool_atomic_fast.txt": toolkit / "src/core/threading/atomic_pool/LICENSE",
    }.items():
        shutil.copy2(source, notices / name)
    (destination / "README.txt").write_text(
        "Pocket Library\n\nA reading room of 746 books, each set as a printed book on the paper\n"
        "you choose and opened to the page you left. Search by title or author, click a\n"
        "spine to read; Help is the ? at the top right.\n"
        "Your places are kept per user, separate from this application folder.\n\n"
        "Source and release notes: https://github.com/malkuth-software/books\n"
        "Its companion, PlaySuite (games): https://github.com/malkuth-software/games\n"
        "Each book's edition and licence is on its About card. Font licenses accompany the\n"
        "fonts; other dependency notices are in licenses/.\n\n"
        "Crafted by Anthropic Claude General Reasoning Agents and OpenAI Codex General\n"
        "Reasoning Agents.\nSponsor: Rainstar\n", encoding="utf-8")


def write_manifest(bundle, system, architecture):
    files = {}
    for path in sorted(bundle.rglob("*")):
        if path.is_file():
            files[path.relative_to(bundle).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    manifest = {"version": project_version(), "source_revision": revision,
                "system": system, "architecture": architecture,
                "toolkit_source_revision": TOOLKIT_REVISION, "files": files}
    (bundle / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
