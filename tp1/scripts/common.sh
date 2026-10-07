# Variables et fonctions partagées par les scripts. Ne pas exécuter directement : `source` seulement.
#
# Variables d'environnement reconnues :
#   ASAN=1          compile avec AddressSanitizer dans build-asan/ au lieu de build/
#   DATA_DIR=...    dossier d'images utilisé par les run_*.sh (défaut : data/downloaded)

set -euo pipefail

# Racine du projet tp1/, peu importe d'où le script est lancé.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ "${ASAN:-0}" == "1" ]]; then
    BUILD_DIR="build-asan"
    CMAKE_FLAGS=(-DENABLE_ASAN=ON)
else
    BUILD_DIR="build"
    CMAKE_FLAGS=()
fi

DATA_DIR="${DATA_DIR:-data/downloaded}"

# Configure (si nécessaire) puis compile les cibles demandées (toutes si aucune).
build() {
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        echo "==> Configuration de $BUILD_DIR/ (la 1re fois, TBB est téléchargé : quelques minutes)"
        cmake -B "$BUILD_DIR" -G Ninja ${CMAKE_FLAGS[@]+"${CMAKE_FLAGS[@]}"}
    fi
    echo "==> Compilation ($BUILD_DIR/)"
    if [[ $# -gt 0 ]]; then
        cmake --build "$BUILD_DIR" --target "$@"
    else
        cmake --build "$BUILD_DIR"
    fi
}

# Vérifie qu'il y a des images à traiter.
require_images() {
    if ! compgen -G "$DATA_DIR/*.png" >/dev/null && ! compgen -G "$DATA_DIR/*.jpg" >/dev/null; then
        echo "Aucune image dans $DATA_DIR/. Lancez d'abord : ./data/fetch.py 20" >&2
        exit 1
    fi
}

# run_pipeline <exécutable> <serial|pthread|tbb|all> [arguments supplémentaires...]
run_pipeline() {
    local exe="$1" pipeline="$2"
    shift 2  # le reste de "$@" = arguments supplémentaires pour l'exécutable
    build "$exe"
    require_images
    echo "==> $BUILD_DIR/$exe --directory $DATA_DIR --pipeline $pipeline $*"
    "$BUILD_DIR/$exe" --directory "$DATA_DIR" --pipeline "$pipeline" "$@"
}
