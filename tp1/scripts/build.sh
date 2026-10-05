#!/usr/bin/env bash
# Configure (si nécessaire) et compile le projet.
#
#   ./scripts/build.sh                  compile tout dans build/
#   ./scripts/build.sh pipeline-notbb   compile seulement cette cible
#   ./scripts/build.sh --clean          supprime build/ puis reconfigure et compile
#   ASAN=1 ./scripts/build.sh           compile avec AddressSanitizer dans build-asan/
source "$(dirname "$0")/common.sh"

if [[ "${1:-}" == "--clean" ]]; then
    echo "==> Suppression de $BUILD_DIR/"
    rm -rf "$BUILD_DIR"
    shift
fi

build "$@"
