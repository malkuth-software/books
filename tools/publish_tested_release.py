#!/usr/bin/env python3
"""Publish already-tested artifacts, attaching a tag only to the tested commit.

Workflow job dependencies are the test gate. Draft staging prevents failed
uploads exposing a partial public release. Reruns are idempotent for the same
version/commit; conflicting existing tags are always rejected.
"""
import argparse
import hashlib
import json
import re
import os
from pathlib import Path
import subprocess
import tempfile
from publication_version import validate


def gh(*arguments):
    return subprocess.check_output(["gh", *arguments], text=True).strip()


def release_notes(root, version, sha, repo):
    lines = [f"Pocket Library {version}.", "",
             f"Built and tested from [commit `{sha[:12]}`](https://github.com/{repo}/commit/{sha}).", "",
             (root / "packaging/RELEASE_NOTES.md").read_text(encoding="utf-8").strip(), ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--test-results", type=Path, help="test-results-*.json to carry beside the packages")
    args = parser.parse_args()
    repo, sha = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"]
    version, tag = os.environ["RELEASE_VERSION"], os.environ["RELEASE_TAG"]
    validate(version)
    if tag != "v" + version:
        raise ValueError("Release tag/version mismatch")
    # An older slow main run must not publish over a newer main revision.
    if os.environ["GITHUB_REF"] == "refs/heads/main":
        if gh("api", f"repos/{repo}/commits/main", "--jq", ".sha") != sha:
            print("A newer main revision exists; retain this run's tested artifacts without publication.")
            return
    if gh("api", f"repos/{repo}/commits/{sha}", "--jq", ".sha") != sha:
        raise ValueError("Tested commit cannot be resolved")
    files = sorted(path for path in args.artifacts.iterdir() if path.is_file() and path.name != "SHA256SUMS.txt")
    expected_suffixes = ("-windows-x64.zip", "-windows-x64-setup.exe", "-macos-arm64.zip", "-macos-arm64.pkg",
                         "-linux-amd64.tar.gz", "-linux-amd64.deb", "-linux-arm64.tar.gz", "-linux-arm64.deb")
    expected = {"pocketlibrary-" + version + suffix for suffix in expected_suffixes}
    if {path.name for path in files} != expected:
        raise ValueError("Incomplete or unexpected tested platform package set")
    sums = args.artifacts / "SHA256SUMS.txt"
    sums.write_text("".join(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n" for path in files))
    files.append(sums)
    # The test results (tools/test_reuse.py) travel with the release; they are not packages.
    if args.test_results and args.test_results.is_dir():
        for path in sorted(args.test_results.glob("test-results-*.json")):
            if re.fullmatch(r"test-results-(app|core)-[a-z0-9-]+\.json", path.name):
                files.append(path)
    # A missing ref is HTTP 404; the commits endpoint returns HTTP 422 for an
    # unresolved name. Probe the ref, then peel annotated tags with commits.
    probe = subprocess.run(["gh", "api", f"repos/{repo}/git/ref/tags/{tag}"], text=True, capture_output=True)
    if probe.returncode == 0:
        if gh("api", f"repos/{repo}/commits/{tag}", "--jq", ".sha") != sha:
            raise ValueError("Existing release tag points to an untested/different commit")
    elif "404" not in probe.stderr:
        raise RuntimeError("Cannot safely check release tag: " + probe.stderr)
    else:
        # Create the immutable tested tag explicitly. GitHub refuses conflicting
        # refs atomically; never let release creation silently reuse another SHA.
        gh("api", f"repos/{repo}/git/refs", "--method", "POST",
           "-f", "ref=refs/tags/" + tag, "-f", "sha=" + sha)
    if gh("api", f"repos/{repo}/commits/{tag}", "--jq", ".sha") != sha:
        raise ValueError("Release tag changed before publication")
    release = subprocess.run(["gh", "release", "view", tag, "--repo", repo, "--json", "isDraft,targetCommitish"], text=True, capture_output=True)
    if release.returncode == 0:
        existing = json.loads(release.stdout)
        if probe.returncode != 0 and (not existing["isDraft"] or existing["targetCommitish"] != sha):
            raise RuntimeError("Existing release has no matching tested target")
        if not existing["isDraft"]:
            print("Matching tested release already published; leaving it immutable.")
            return
    else:
        # gh create refuses an existing release; permission/network errors fail closed.
        with tempfile.TemporaryDirectory(prefix="pocketlibrary-release-") as directory:
            notes = Path(directory) / "notes.md"
            notes.write_text(release_notes(Path(__file__).resolve().parents[1], version, sha, repo), encoding="utf-8")
            gh("release", "create", tag, "--repo", repo, "--target", sha, "--draft",
               "--title", "Pocket Library " + version, "--notes-file", str(notes))
    gh("release", "upload", tag, "--repo", repo, "--clobber", *(str(path) for path in files))
    gh("release", "edit", tag, "--repo", repo, "--draft=false",
       "--latest=" + str(os.environ["GITHUB_REF"] == "refs/heads/main").lower())


if __name__ == "__main__":
    main()
