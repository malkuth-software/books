"""Shared app resources and notices, read from an installed GUI.Forms SDK prefix (--toolkit)."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# PLACEHOLDER PIN (claude/conformance-prep): still v0.2.1308. The sources target GUI.Forms
# main 96d3696 (0.6); the pin moves at the conformance rebase (CONFORMANCE_PREP.md).
TOOLKIT_REVISION = "e077f9d5a5117312e65e8c3b741c4edd24013d56"


def sdk_notices(prefix):
    """GUI.Forms' licences in an installed SDK prefix (`cmake --install` of GUI.Forms, or
    its tools/build_sdk.py): share/licenses/GUIForms, every notice for what the SDK links
    (its stx packages and STXlib, Skia, Unicode, miniaudio, BFFT, the
    LLVM runtime on Linux) under the names its install gives them."""
    notices = prefix / "share/licenses/GUIForms"
    if not (notices / "LICENSE").is_file():
        raise RuntimeError(str(prefix) + " is not an installed GUI.Forms SDK prefix "
                           "(no share/licenses/GUIForms/LICENSE); pass --toolkit <prefix>")
    return notices


def sdk_fonts(prefix):
    """The SDK's bundled fonts (GUIForms_FONT_DIR): share/GUIForms/fonts."""
    fonts = prefix / "share/GUIForms/fonts"
    if not fonts.is_dir():
        raise RuntimeError(str(prefix) + " has no share/GUIForms/fonts")
    return fonts


def miniaudio_license(notices, build, cache):
    """miniaudio's licence: the SDK's copy first; otherwise the source the configure used,
    named by FETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO, under FETCHCONTENT_BASE_DIR, or in
    the build's _deps. Raises naming every place looked at."""
    candidates = [notices / "miniaudio-LICENSE.txt"]
    override = re.search(r"^FETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO:PATH=(.+)$", cache, re.MULTILINE)
    if override:
        candidates.append(Path(override.group(1).strip()) / "LICENSE")
    base = re.search(r"^FETCHCONTENT_BASE_DIR:PATH=(.+)$", cache, re.MULTILINE)
    if base:
        candidates.append(Path(base.group(1).strip()) / "gui_forms_miniaudio-src/LICENSE")
    candidates.append(build / "_deps/gui_forms_miniaudio-src/LICENSE")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("miniaudio's licence not found; looked at " + ", ".join(str(c) for c in candidates))


def source_revision(given):
    """The packaged source commit: `given` (--source-revision) when set, else this checkout's
    HEAD through git; a package built from an exported tree passes it."""
    if given:
        return given
    try:
        return subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError("No git checkout at " + str(ROOT) + "; pass --source-revision") from error


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
    shutil.copytree(sdk_fonts(toolkit), destination / "fonts", dirs_exist_ok=True)
    notices = destination / "licenses"
    notices.mkdir(exist_ok=True)
    # GUI.Forms and everything it links, as its installed SDK carries the notices.
    toolkit_notices = sdk_notices(toolkit)
    shutil.copytree(toolkit_notices, notices / "GUIForms", dirs_exist_ok=True)
    shutil.copy2(miniaudio_license(toolkit_notices, build, cache), notices / "miniaudio.txt")
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


def write_manifest(bundle, system, architecture, revision=None):
    files = {}
    for path in sorted(bundle.rglob("*")):
        if path.is_file():
            files[path.relative_to(bundle).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"version": project_version(), "source_revision": source_revision(revision),
                "system": system, "architecture": architecture,
                "toolkit_source_revision": TOOLKIT_REVISION, "files": files}
    (bundle / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
