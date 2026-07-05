#!/bin/bash
set -e

patch_submodule() {
    local SUBMODULE_PATH="$(realpath $1)"
    local SUBMODULE_SHORT="$(basename $SUBMODULE_PATH)"
    local PATCHES_DIR="$(realpath $2)"
    local OLD_PWD="$PWD"

    # This extracts the pinned commit hash from the parent repository's index
    local EXPECTED_COMMIT=$(git ls-tree HEAD "$SUBMODULE_PATH" | awk '{print $3}')

    echo "Resetting submodule to expected local commit: $EXPECTED_COMMIT"
    cd "$SUBMODULE_PATH"

    # 1. Forcefully discard any previous applied patches or local edits
    git reset --hard "$EXPECTED_COMMIT"
    git clean -fd

    # 2. Re-apply your custom patches locally
    for patch in "$PATCHES_DIR"/*.patch; do
        if [ -f "$patch" ]; then
            echo "Applying: $(basename "$patch")"
            git apply "$patch"
        fi
    done

    echo "Submodule $SUBMODULE_SHORT patched"
    cd "$OLD_PWD"
}

patch_vendor() {
    local VENDOR="$1"
    patch_submodule "vendor/$VENDOR" "vendor/patches/$VENDOR"
}

for vendor in vendor/patches/*/; do
    patch_vendor "$(basename "$vendor")"
done
