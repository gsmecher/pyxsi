#!/bin/bash
#
# provision-libs.sh -- copy Vivado runtime libraries into a local directory,
# strip problematic DT_NEEDED entries, and pull in the transitive closure.
#
# Usage: provision-libs.sh <vivado_libdir> <output_dir>

set -euo pipefail

VIVADO_LIBDIR="$1"
OUTDIR="$2"

# Libraries whose DT_NEEDED entries cause conflicts when loaded into a
# host Python process. These are stripped from every .so we copy, and
# the libraries themselves are deleted from the output directory.
STRIP_NEEDED=(
    libpython3.13.so.1.0
    libssl.so.10
    libcrypto.so.10
)

# Find kernel libraries (librdi_ before 2025.1, libxv_ from 2025.1 onward)
kernel_libs=()
for pattern in lib{xv,rdi}_simulator_kernel.so lib{xv,rdi}_simbridge_kernel.so; do
    for f in "$VIVADO_LIBDIR"/$pattern; do
        [ -f "$f" ] && kernel_libs+=("$f")
    done
done

if [ ${#kernel_libs[@]} -eq 0 ]; then
    echo "error: no simulator kernel libraries found in $VIVADO_LIBDIR" >&2
    exit 1
fi

mkdir -p "$OUTDIR"

# Step 1: Copy kernel libraries
for src in "${kernel_libs[@]}"; do
    echo "  copy $(basename "$src")"
    cp "$src" "$OUTDIR/$(basename "$src")"
done

# Step 2: Copy the transitive closure of Vivado-provided dependencies
for src in "${kernel_libs[@]}"; do
    LD_LIBRARY_PATH="$VIVADO_LIBDIR" ldd "$src" \
        | grep "$VIVADO_LIBDIR" \
        | awk '{print $3}' \
        | while read -r f; do
            dst="$OUTDIR/$(basename "$f")"
            if [ ! -f "$dst" ]; then
                echo "  copy $(basename "$f")"
                cp "$f" "$dst"
            fi
        done
done

# Step 3: Strip problematic DT_NEEDED entries from everything
for f in "$OUTDIR"/*.so*; do
    [ -f "$f" ] || continue
    for lib in "${STRIP_NEEDED[@]}"; do
        patchelf --remove-needed "$lib" "$f" 2>/dev/null || true
    done
done

# Step 4: Delete the problematic libraries themselves
for lib in "${STRIP_NEEDED[@]}"; do
    rm -f "$OUTDIR/$lib"
done

echo "  done ($(ls "$OUTDIR"/*.so* 2>/dev/null | wc -l) libraries in $OUTDIR)"
