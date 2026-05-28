#!/usr/bin/env bash
# scripts/update-gui-hashes.sh — apply real per-asset SHA-256 hashes to the
# GUI auto-downloader manifest and tidy the release notes.
#
# Invoked at release time, after the four platform release artifacts are
# built (Linux x64, macOS ARM64 CLI tarball, Windows x64 zip, Docker image).
# Reads a SHA256SUMS.txt-style input on stdin or as an argument and:
#
#   1. Patches the four PLACEHOLDER_SHA256_UPDATE_AT_RELEASE entries in
#      gui/radiant_node_web.py with the real values, matched by filename.
#
#   2. Removes the "GUI release-asset manifest ships with placeholder
#      SHA-256 values" bullet from doc/release-notes/release-notes-3.0.0.md
#      because at release time it's no longer a limitation.
#
# Idempotent: running twice with the same input is a no-op (no diff).
#
# Usage:
#   scripts/update-gui-hashes.sh releases/v3.0.0/SHA256SUMS.txt
#
#   # or pipe from sha256sum directly:
#   cd releases/v3.0.0 && sha256sum *.tar.gz *.zip \
#       | ../../scripts/update-gui-hashes.sh -
#
# Expected SHA256SUMS.txt format (one entry per line, `<hash>  <filename>`):
#   abc...123  radiant-core-linux-x64-v3.0.0.tar.gz
#   def...456  radiant-core-macos-arm64-v3.0.0.tar.gz
#   789...abc  radiant-core-windows-x64-v3.0.0.zip
#
# The script matches filenames flexibly — version suffix is stripped so the
# manifest entries (which use unsuffixed filenames like
# "radiant-core-linux-x64.tar.gz") match regardless of the version tag.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
MANIFEST="${ROOT_DIR}/gui/radiant_node_web.py"
RELEASE_NOTES="${ROOT_DIR}/doc/release-notes/release-notes-3.0.0.md"

INPUT="${1:-}"
if [ -z "${INPUT}" ] || [ "${INPUT}" = "-" ]; then
    INPUT="$(mktemp)"
    cat > "${INPUT}"
    trap 'rm -f "${INPUT}"' EXIT
fi

if [ ! -f "${INPUT}" ]; then
    echo "ERROR: SHA256SUMS file not found: ${INPUT}" >&2
    exit 1
fi
if [ ! -f "${MANIFEST}" ]; then
    echo "ERROR: GUI manifest not found: ${MANIFEST}" >&2
    exit 1
fi

# Build an associative array of: unsuffixed-filename -> sha256.
# The manifest uses unsuffixed filenames (`radiant-core-linux-x64.tar.gz`)
# but release artifacts have a version suffix (`...-v3.0.0.tar.gz`). Strip
# `-v*.<ext>` to canonicalize.
declare -A HASH_BY_FILE
while read -r line; do
    [ -z "${line}" ] && continue
    hash="$(echo "${line}" | awk '{print $1}')"
    file="$(echo "${line}" | awk '{print $NF}')"
    file="$(basename "${file}")"
    # Strip "-vX.Y.Z" before the extension. Handles both .tar.gz and .zip.
    canonical="$(echo "${file}" | sed -E 's/-v[0-9]+\.[0-9]+\.[0-9]+(\.tar\.gz|\.zip)$/\1/')"
    HASH_BY_FILE["${canonical}"]="${hash}"
done < "${INPUT}"

if [ "${#HASH_BY_FILE[@]}" -eq 0 ]; then
    echo "ERROR: no entries parsed from ${INPUT}" >&2
    exit 1
fi

echo "Parsed hashes:"
for f in "${!HASH_BY_FILE[@]}"; do
    echo "  ${f}  ${HASH_BY_FILE[$f]}"
done
echo

# --- patch the manifest -------------------------------------------------------
# Use Python rather than sed/awk: the RELEASE_ASSETS structure is a Python
# dict; safest to parse it as Python text, walk each platform block, and
# substitute the sha256 string in place. We keep the file's existing
# formatting and only touch the four sha256 fields.
python3 - "${MANIFEST}" <<EOF
import re, sys
manifest_path = sys.argv[1]
hashes = {
$(for f in "${!HASH_BY_FILE[@]}"; do printf '    "%s": "%s",\n' "$f" "${HASH_BY_FILE[$f]}"; done)
}

with open(manifest_path, "r", encoding="utf-8") as f:
    src = f.read()

# Walk each platform block. Each block looks like:
#   {
#       "filename": "radiant-core-linux-x64.tar.gz",
#       ...
#       "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE",
#       ...
#   }
# We match a filename line followed (within ~10 lines) by a sha256 line
# and replace the placeholder.
pat = re.compile(
    r'("filename":\s*"(?P<file>[^"]+)".*?"sha256":\s*")(?P<hash>[^"]+)(")',
    re.DOTALL,
)

def sub(m):
    fn = m.group("file")
    new_hash = hashes.get(fn)
    if new_hash is None:
        # Unknown filename — leave existing hash (could be a known-good
        # entry from a previous run; we don't overwrite).
        return m.group(0)
    return f'{m.group(1)}{new_hash}{m.group(4)}'

new_src, n = pat.subn(sub, src)
if n == 0:
    print("WARNING: no RELEASE_ASSETS entries matched in the manifest.", file=sys.stderr)
if new_src != src:
    with open(manifest_path, "w", encoding="utf-8") as f:
        f.write(new_src)
    print(f"Updated {n} entries in {manifest_path}", file=sys.stderr)
else:
    print(f"{manifest_path} already current (no changes).", file=sys.stderr)
EOF

# --- tidy the release notes ---------------------------------------------------
# Drop the "GUI release-asset manifest ... ships with placeholder SHA-256
# values" bullet from the Known Limitations section. We match the leading
# bullet text and remove the whole paragraph (a markdown bullet can span
# multiple physical lines until the next blank line or next bullet).
if [ -f "${RELEASE_NOTES}" ] && grep -q "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE" "${RELEASE_NOTES}"; then
    python3 - "${RELEASE_NOTES}" <<'EOF'
import re, sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    src = f.read()

# Find the Known Limitations section and drop the GUI-placeholder bullet.
pat = re.compile(
    r'^- \*\*GUI release-asset manifest\*\*.*?(?=^- \*\*|^---|^##|\Z)',
    re.MULTILINE | re.DOTALL,
)
new_src = pat.sub('', src)
# Collapse any double-blank-lines we introduced.
new_src = re.sub(r'\n{3,}', '\n\n', new_src)
if new_src != src:
    with open(path, "w", encoding="utf-8") as f:
        f.write(new_src)
    print(f"Removed GUI-placeholder bullet from {path}", file=sys.stderr)
else:
    print(f"{path} already current.", file=sys.stderr)
EOF
fi

echo
echo "Done. Review changes:"
echo "  git -C $(realpath --relative-to="${PWD}" "${ROOT_DIR}") diff -- gui/radiant_node_web.py doc/release-notes/release-notes-3.0.0.md"
