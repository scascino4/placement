#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
benchmark="$repo_root/data/ispd2005/adaptec1/adaptec1.dp.aux"
build_dir=${VALGRIND_BUILD_DIR:-build/valgrind}

if ! command -v valgrind >/dev/null 2>&1; then
    echo "valgrind is not installed" >&2
    exit 1
fi

if [[ ! -f "$benchmark" ]]; then
    echo "adaptec1 benchmark not found: $benchmark" >&2
    exit 1
fi

if [[ "$build_dir" = /* ]]; then
    build_path=$build_dir
else
    build_path="$repo_root/$build_dir"
fi

debug_cxxflags=${VALGRIND_CXXFLAGS:--std=c++23 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow}
"${MAKE:-make}" -C "$repo_root" BUILD_DIR="$build_dir" \
    CXXFLAGS="$debug_cxxflags" all

parse_bin="$build_path/bin/placement_parse"
render_bin="$build_path/bin/placement_render"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/placement-valgrind.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT

valgrind_flags=(
    --tool=memcheck
    --leak-check=full
    --show-leak-kinds=all
    --track-origins=yes
    --errors-for-leak-kinds=all
    --error-exitcode=99
)

if [[ -n ${VALGRIND_FLAGS:-} ]]; then
    read -r -a valgrind_flags <<< "$VALGRIND_FLAGS"
fi

binary="$work_dir/adaptec1.placebin"
svg="$work_dir/adaptec1.svg"
utilization_svg="$work_dir/adaptec1-utilization.svg"
pin_density_svg="$work_dir/adaptec1-pin-density.svg"

echo "Running parser under Valgrind (adaptec1)..."
valgrind "${valgrind_flags[@]}" "$parse_bin" "$benchmark" "$binary"

echo "Running renderer under Valgrind (adaptec1)..."
valgrind "${valgrind_flags[@]}" "$render_bin" "$binary" "$svg"

echo "Running utilization renderer under Valgrind (adaptec1)..."
valgrind "${valgrind_flags[@]}" "$render_bin" --output-format utilization-svg \
    "$binary" "$utilization_svg"

echo "Running pin density renderer under Valgrind (adaptec1)..."
valgrind "${valgrind_flags[@]}" "$render_bin" --output-format pin-density-svg \
    "$binary" "$pin_density_svg"

echo "Valgrind smoke test passed."
