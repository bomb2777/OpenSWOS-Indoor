#!/usr/bin/env bash
# Windows PC (Cygwin64 terminal): bring the fork's branch up to date from GitHub, build the
# game and start it. The Windows twin of the Linux PC's update_and_run_openswos.sh -- but
# upstream is NOT merged here: the Linux PC rebases `indoor` onto upstream and force-pushes,
# and this PC only follows origin/indoor.
#
#   1. the working tree checked (an uncommitted en.json is thrown away, anything else stops)
#   2. git pull --rebase from origin (survives the force-push after a rebase)
#   3. submodules updated
#   4. en.json regenerated from the C strings
#   5. the game built: make SWOS_SAVE_FORMAT_JSON_VERIFY=0 -> bin/SWOS.exe
#   6. the game started
#
# Run from the checkout:  scripts/indoor_windows_run.sh [--no-run|--no-build] [game options ...]
#   --no-run          stop after the build
#   --no-build        only start the game as it is built (a second copy for a local online
#                     test: one OPENs, the other JOINs 127.0.0.1)
#   anything else     passed to the game, e.g. --dos-gameplay=on
# Overrides: BRANCH=<branch> (default indoor)
# For one run indoors: SWOS_INDOOR=1 scripts/indoor_windows_run.sh
#
# The data setup (openswos-setup.exe) is NOT built here: it needs the MSVC prompt
# (tools\swosdata\build.bat). Run it again only when it tells you the data needs updating.

set -euo pipefail

# Everything is inside main(), which bash reads whole before running it: step 2 may pull a
# new version of this very file, and a script bash is still reading must not change under it.
main() {
    local REPO BRANCH EN_JSON RUN=1 BUILD=1 GAME_ARGS=() a
    REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    BRANCH="${BRANCH:-indoor}"
    EN_JSON="bin/data_json/translations/en.json"

    for a in "$@"; do
        case "$a" in
            --no-run)   RUN=0 ;;
            --no-build) BUILD=0 ;;
            -h|--help)  sed -n '2,24p' "${BASH_SOURCE[0]}"; exit 0 ;;
            *)          GAME_ARGS+=("$a") ;;
        esac
    done

    case "$(uname -s)" in
        CYGWIN*) ;;
        *) die "run this in the Cygwin64 terminal (uname says $(uname -s))" ;;
    esac

    cd "$REPO" || die "no checkout at $REPO"

    if [ "$BUILD" = 0 ]; then
        [ -x bin/SWOS.exe ] || die "no bin/SWOS.exe yet -- run once without --no-build"
        step "Starting the game (no update, no build: $(git log --oneline -1))"
        cd bin
        exec ./SWOS.exe "${GAME_ARGS[@]}"
    fi

    # --- 1. clean working tree ------------------------------------------------
    step "Checking the working tree"
    if ! git diff --quiet -- "$EN_JSON"; then
        echo "discarding the uncommitted $EN_JSON (it is regenerated below)"
        git checkout -- "$EN_JSON"
    fi
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        git status --short --untracked-files=no
        die "uncommitted changes -- commit or stash them first"
    fi
    if [ -d .git/rebase-merge ] || [ -d .git/rebase-apply ]; then
        die "a rebase is still in progress -- finish it or 'git rebase --abort'"
    fi

    # --- 2. pull ----------------------------------------------------------------
    step "Pulling $BRANCH from origin (rebase)"
    git checkout -q "$BRANCH"
    local before; before="$(git rev-parse HEAD)"
    if ! git pull --rebase origin "$BRANCH"; then
        die "the pull stopped on a conflict. This PC normally has no commits of its own:
to simply take GitHub's branch, run
  git rebase --abort && git reset --hard origin/$BRANCH
(that throws away commits made only on this PC), then run this script again."
    fi
    if [ "$before" = "$(git rev-parse HEAD)" ]; then
        echo "already up to date: $(git log --oneline -1)"
    else
        echo "now: $(git log --oneline -1)"
    fi
    if [ "$(git rev-list --count "origin/$BRANCH..HEAD")" != 0 ]; then
        echo "note: this PC has commits GitHub does not:"
        git log --oneline "origin/$BRANCH..HEAD"
    fi

    # --- 3. submodules ------------------------------------------------------------
    step "Updating submodules"
    git submodule update --init --recursive

    # --- 4. translations ------------------------------------------------------------
    step "Regenerating $EN_JSON"
    python3 scripts/export_translations.py
    git diff --quiet -- "$EN_JSON" && echo "en.json is up to date" \
        || echo "(en.json changed -- left uncommitted; the next run throws it away)"

    # --- 5. the game ------------------------------------------------------------------
    step "Building the game (bin/SWOS.exe)"
    make -j"$(nproc)" SWOS_SAVE_FORMAT_JSON_VERIFY=0
    echo "built: $(git log --oneline -1)"

    # --- 6. play ------------------------------------------------------------------------
    if [ "$RUN" = 1 ]; then
        step "Starting the game (log: Documents\\SWOS\\SWOS.log)"
        cd bin
        exec ./SWOS.exe "${GAME_ARGS[@]}"
    fi
    echo "run:   cd $REPO/bin && ./SWOS.exe"
}

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

main "$@"
exit
