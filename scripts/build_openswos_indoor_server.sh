#!/usr/bin/env bash
# build_openswos_indoor_server.sh -- (OpenSWOS-Indoor) the matchmaking server for the owner's VPS
# (Debian 13), built here; the archive goes to the owner's SFTP, from where Ansible installs it
# (role game_servers, tasks/openswos_indoor_server.yml): no compiler on the server.
#
#   scripts/build_openswos_indoor_server.sh 0.0.1
#       -> dist/openswos_indoor_server-0.0.1.tar.gz
#          (one folder inside: openswos_indoor_server, VERSION, COMMIT; plus a .sha256 beside it)
#   The version is the owner's, set by hand (Ansible: openswos_indoor_server_version); the commit it was
#   built from is in COMMIT.
#   MAX_GLIBC=2.41 scripts/build_openswos_indoor_server.sh 0.0.1   (the target's glibc; Debian 13: 2.41)
#
# The C++ runtime and OpenSSL are linked statically; glibc stays dynamic (its name lookup,
# which the HTTPS calls need, cannot be linked in), and so does libzstd (the static libcrypto
# pulls it in; Debian has libzstd1). A binary built on a newer system can still need glibc
# symbols the target lacks, so the script reads the highest GLIBC_ version the binary asks for
# and FAILS above MAX_GLIBC -- then build in a container of the target's system instead.
# A tree with uncommitted changes is refused unless ALLOW_DIRTY=1 (COMMIT then ends in -dirty).
#
# Needs here, once: sudo apt install cmake g++ libssl-dev libzstd-dev libjitterentropy3-dev
# (Ubuntu 26.04's static libcrypto.a links jitterentropy and zstd)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
MAX_GLIBC="${MAX_GLIBC:-2.41}"
VERSION="${1:-}"
[ -n "$VERSION" ] || { echo "usage: $0 <version>   e.g. $0 0.0.1" >&2; exit 1; }
VERSION="${VERSION#v}"
[[ "$VERSION" =~ ^[0-9A-Za-z._-]+$ ]] || { echo "version: letters, digits, . _ - only" >&2; exit 1; }
DIRTY=""
git diff --quiet HEAD -- . ':!bin/data_json/translations/en.json' || DIRTY="-dirty"
NAME="openswos_indoor_server-$VERSION"
BUILD="build/openswos_indoor_server"
STAGE="$BUILD/stage/$NAME"
OUT="dist/$NAME.tar.gz"

if [ -n "$DIRTY" ] && [ "${ALLOW_DIRTY:-0}" != 1 ]; then
    echo "uncommitted changes: commit first, or ALLOW_DIRTY=1 for a test build" >&2; exit 1
fi
command -v cmake >/dev/null || { echo "cmake not found: sudo apt install cmake" >&2; exit 1; }
ls /usr/lib/*/libssl.a >/dev/null 2>&1 || { echo "no static OpenSSL (libssl.a): sudo apt install libssl-dev libzstd-dev libjitterentropy3-dev" >&2; exit 1; }

rm -rf "$BUILD"
cmake -S third_party/matchmaker -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
      -DOPENSSL_USE_STATIC_LIBS=TRUE \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >/dev/null
cmake --build "$BUILD" --target swos-matchmaker -j"$(nproc)" >/dev/null
mkdir -p "$STAGE" dist
cp "$BUILD/swos-matchmaker" "$STAGE/openswos_indoor_server"
strip "$STAGE/openswos_indoor_server"
BIN="$STAGE/openswos_indoor_server"

echo "== $NAME ($(ls -lh "$BIN" | awk '{print $5}'))"
echo "-- dynamic libraries:"
ldd "$BIN" | sed 's/^/   /'
if ldd "$BIN" | grep -Eq 'libssl|libcrypto|libstdc\+\+'; then
    echo "ERROR: OpenSSL or libstdc++ is still linked dynamically" >&2
    exit 1
fi
need=$(objdump -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)
echo "-- needs glibc $need at the most (target: $MAX_GLIBC)"
if [ "$(printf '%s\n%s\n' "$need" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
    echo "ERROR: needs glibc $need, newer than the target's $MAX_GLIBC" >&2
    objdump -T "$BIN" | grep "GLIBC_$need" | sed 's/^/   /' >&2
    exit 1
fi

echo "$VERSION" > "$STAGE/VERSION"
echo "$(git rev-parse HEAD)$DIRTY" > "$STAGE/COMMIT"
tar -C "$BUILD/stage" --owner=0 --group=0 -czf "$OUT" "$NAME"
(cd dist && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "-- $OUT"
cat "$OUT.sha256"
