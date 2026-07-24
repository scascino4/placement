#!/usr/bin/env bash

set -euo pipefail

die() {
    echo "benchmark-run: $*" >&2
    exit 1
}

require_file() {
    [[ -f $1 ]] || die "missing '$1'; run ./scripts/prepare_data.sh and retry"
}

file_size() {
    local size
    size=$(wc -c <"$1")
    echo "${size//[[:space:]]/}"
}

record_command() {
    local family=$1
    local design=$2
    local run=$3
    local operation=$4
    shift 4

    local output stage seconds units checksum row
    output=$("$@")
    [[ -n $output ]] || die "'$operation' produced no benchmark result"

    while IFS=$'\t' read -r stage seconds units checksum; do
        [[ -n $stage && -n $seconds && -n $units && -n $checksum ]] ||
            die "'$operation' produced a malformed benchmark result"
        printf -v row '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
            "$family" "$design" "$run" "$operation" \
            "$stage" "$seconds" "$units" "$checksum"
        printf '%s\n' "$row"
        printf '%s\n' "$row" >&3
    done <<<"$output"
}

warm_command() {
    "$@" >/dev/null
}

run_command() {
    local mode=$1
    shift
    if [[ $mode == warm ]]; then
        shift 4
        warm_command "$@"
    else
        record_command "$@"
    fi
}

run_bookshelf() {
    local mode=$1
    local run=$2
    local format output

    run_command "$mode" bookshelf "$bookshelf_design" "$run" input_scan \
        "$benchmark" scan "${bookshelf_inputs[@]}"
    run_command "$mode" bookshelf "$bookshelf_design" "$run" parse \
        "$benchmark" bookshelf "$bookshelf_aux" "$bookshelf_placebin"
    run_command "$mode" bookshelf "$bookshelf_design" "$run" binary \
        "$benchmark" binary "$bookshelf_placebin" "$bookshelf_copy"
    run_command "$mode" bookshelf "$bookshelf_design" "$run" analysis \
        "$benchmark" analysis "$bookshelf_placebin"

    for format in svg utilization-svg pin-density-svg cell-density-svg; do
        output="$benchmark_dir/bookshelf-$bookshelf_design-$format.svg"
        run_command "$mode" bookshelf "$bookshelf_design" "$run" "render_$format" \
            "$benchmark" render "$format" "$bookshelf_placebin" "$output"
    done

    run_command "$mode" bookshelf "$bookshelf_design" "$run" raw_write_placebin \
        "$benchmark" write "$(file_size "$bookshelf_placebin")" "$raw_output"
    for format in svg utilization-svg pin-density-svg cell-density-svg; do
        output="$benchmark_dir/bookshelf-$bookshelf_design-$format.svg"
        run_command "$mode" bookshelf "$bookshelf_design" "$run" "raw_write_$format" \
            "$benchmark" write "$(file_size "$output")" "$raw_output"
    done
}

run_lefdef() {
    local mode=$1
    local run=$2
    local format output

    run_command "$mode" lefdef "$lefdef_design" "$run" input_scan \
        "$benchmark" scan "$lefdef_tech" "$lefdef_cells" "$lefdef_def"
    run_command "$mode" lefdef "$lefdef_design" "$run" parse \
        "$benchmark" lefdef "$lefdef_def" "$lefdef_placebin" \
        "$lefdef_tech" "$lefdef_cells"
    run_command "$mode" lefdef "$lefdef_design" "$run" binary \
        "$benchmark" binary "$lefdef_placebin" "$lefdef_copy"
    run_command "$mode" lefdef "$lefdef_design" "$run" analysis \
        "$benchmark" analysis "$lefdef_placebin"

    for format in svg utilization-svg pin-density-svg cell-density-svg; do
        output="$benchmark_dir/lefdef-$lefdef_design-$format.svg"
        run_command "$mode" lefdef "$lefdef_design" "$run" "render_$format" \
            "$benchmark" render "$format" "$lefdef_placebin" "$output"
    done

    run_command "$mode" lefdef "$lefdef_design" "$run" raw_write_placebin \
        "$benchmark" write "$(file_size "$lefdef_placebin")" "$raw_output"
    for format in svg utilization-svg pin-density-svg cell-density-svg; do
        output="$benchmark_dir/lefdef-$lefdef_design-$format.svg"
        run_command "$mode" lefdef "$lefdef_design" "$run" "raw_write_$format" \
            "$benchmark" write "$(file_size "$output")" "$raw_output"
    done
}

[[ $# -eq 1 ]] || die "usage: bench/run.sh <placement_benchmark>"

readonly benchmark_arg=$1
readonly repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"

if [[ $benchmark_arg == /* ]]; then
    readonly benchmark=$benchmark_arg
else
    readonly benchmark="$repo/$benchmark_arg"
fi
readonly runs=${BENCH_RUNS:-5}
readonly benchmark_dir=${BENCH_DIR:-build/benchmark}
readonly bookshelf_design=${BENCH_BOOKSHELF_DESIGN:-bigblue4}
readonly lefdef_design=${BENCH_LEFDEF_DESIGN:-mgc_superblue12}

[[ $runs =~ ^[1-9][0-9]*$ ]] || die "BENCH_RUNS must be a positive integer"
[[ -x $benchmark ]] || die "benchmark executable '$benchmark' is missing or not executable"

readonly bookshelf_dir="data/ispd2005/$bookshelf_design"
readonly bookshelf_aux="$bookshelf_dir/$bookshelf_design.dp.aux"
require_file "$bookshelf_aux"

declare -a bookshelf_components=()
mapfile -t bookshelf_components < <(
    awk '
        {
            sub(/#.*/, "")
            if (NF >= 3 && $2 == ":") {
                for (i = 3; i <= NF; ++i) {
                    name = $i
                    sub(/\r$/, "", name)
                    print name
                }
                exit
            }
        }
    ' "$bookshelf_aux"
)
[[ ${#bookshelf_components[@]} -ne 0 ]] ||
    die "'$bookshelf_aux' contains no component manifest"
readonly -a bookshelf_components

declare -a bookshelf_inputs=("$bookshelf_aux")
for component in "${bookshelf_components[@]}"; do
    require_file "$bookshelf_dir/$component"
    bookshelf_inputs+=("$bookshelf_dir/$component")
done
readonly -a bookshelf_inputs

readonly lefdef_dir="data/ispd2015/$lefdef_design"
readonly lefdef_tech="$lefdef_dir/tech.lef"
readonly lefdef_cells="$lefdef_dir/cells.lef"
readonly lefdef_def="$lefdef_dir/after_legalized.ntup.fix.def"
require_file "$lefdef_tech"
require_file "$lefdef_cells"
require_file "$lefdef_def"

mkdir -p "$benchmark_dir"
readonly results="$benchmark_dir/results.tsv"
readonly metadata="$benchmark_dir/metadata.txt"
readonly bookshelf_placebin="$benchmark_dir/bookshelf-$bookshelf_design.placebin"
readonly bookshelf_copy="$benchmark_dir/bookshelf-$bookshelf_design-copy.placebin"
readonly lefdef_placebin="$benchmark_dir/lefdef-$lefdef_design.placebin"
readonly lefdef_copy="$benchmark_dir/lefdef-$lefdef_design-copy.placebin"
readonly raw_output="$benchmark_dir/raw-write.tmp"

{
    printf 'timestamp_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'system=%s\n' "$(uname -a)"
    printf 'cxx=%s\n' "${BENCH_CXX:-unknown}"
    printf 'cxxflags=%s\n' "${BENCH_CXXFLAGS:-unknown}"
    printf 'runs=%s\n' "$runs"
    printf 'bookshelf_design=%s\n' "$bookshelf_design"
    printf 'lefdef_design=%s\n' "$lefdef_design"
    printf 'benchmark_dir=%s\n' "$benchmark_dir"
} >"$metadata"

exec 3>"$results"
printf 'family\tdesign\trun\toperation\tstage\tseconds\tunits\tchecksum\n' >&3
printf 'family\tdesign\trun\toperation\tstage\tseconds\tunits\tchecksum\n'

echo "Warming Bookshelf $bookshelf_design..." >&2
run_bookshelf warm warmup
echo "Warming LEF/DEF $lefdef_design..." >&2
run_lefdef warm warmup

for ((run = 1; run <= runs; ++run)); do
    echo "Benchmark run $run/$runs: Bookshelf $bookshelf_design" >&2
    run_bookshelf record "$run"
    echo "Benchmark run $run/$runs: LEF/DEF $lefdef_design" >&2
    run_lefdef record "$run"
done

echo "Benchmark samples: $results" >&2
echo "Benchmark metadata: $metadata" >&2
