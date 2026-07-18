#!/usr/bin/env bash

set -euo pipefail

readonly ISPD2005_DEFAULT_URL="https://www.cerc.utexas.edu/~zixuan/ispd2005dp.tar.xz"
readonly ISPD2005FREE_DEFAULT_URL="https://www.dropbox.com/scl/fi/01jvzui9hv0aa4krnd8lm/ispd2005free.zip?rlkey=ijwspusl9onncnsu5j4na4tqe&st=l44f3dnw&dl=1"
readonly ISPD2015_DEFAULT_URL="https://www.cerc.utexas.edu/~zixuan/ispd2015dp.tar.xz"
readonly -a ISPD2005_DESIGNS=(
    adaptec1 adaptec2 adaptec3 adaptec4
    bigblue1 bigblue2 bigblue3 bigblue4
)
readonly -a ISPD2015_DESIGNS=(
    mgc_des_perf_1 mgc_des_perf_a mgc_des_perf_b mgc_edit_dist_a
    mgc_fft_1 mgc_fft_2 mgc_fft_a mgc_fft_b
    mgc_matrix_mult_1 mgc_matrix_mult_2 mgc_matrix_mult_a
    mgc_matrix_mult_b mgc_matrix_mult_c
    mgc_pci_bridge32_a mgc_pci_bridge32_b
    mgc_superblue11_a mgc_superblue12 mgc_superblue14
    mgc_superblue16_a mgc_superblue19
)
readonly -a ISPD2015_FILES=(
    tech.lef cells.lef floorplan.def after_legalized.ntup.fix.def
    design.v placement.constraints
)

die() {
    echo "error: $*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/prepare_data.sh [options]

Download the ISPD 2005, ISPD 2005 free-macro, and ISPD 2015 benchmarks
into data/. DREAMPlace is run only when an explicit launcher and configuration
are given. ISPD 2015 is prepared for the LEF/DEF parser but is not passed to
DREAMPlace.

Options:
  --placer PATH      Path to dreamplace/Placer.py
  --python PATH      Python interpreter to use with --placer
  --dreamplace PATH  Executable DREAMPlace wrapper (alternative to --placer)
  --config-dir DIR   Directory containing per-design DREAMPlace JSON files
  --design NAME      Place only NAME; may be supplied more than once
  -h, --help         Show this help

The configuration directory must contain ispd2005/<design>.json and
ispd2005free/<design>_allfree.json. Only aux_input and result_dir are
overridden in temporary copies of those files.

Environment:
  ISPD2005_URL       Override the ISPD 2005 archive URL
  ISPD2005FREE_URL   Override the ISPD 2005 free-macro archive URL
  ISPD2015_URL       Override the ISPD 2015 archive URL
EOF
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

is_known_design() {
    local candidate=$1
    local design
    for design in "${ISPD2005_DESIGNS[@]}"; do
        if [[ $candidate == "$design" ]]; then
            return 0
        fi
    done
    return 1
}

ispd2005_family_is_complete() {
    local root=$1
    local suffix=$2
    local design
    for design in "${ISPD2005_DESIGNS[@]}"; do
        [[ -f "$root/$design/${design}${suffix}" ]] || return 1
    done
}

ispd2015_is_complete() {
    local root=$1
    local design file
    for design in "${ISPD2015_DESIGNS[@]}"; do
        for file in "${ISPD2015_FILES[@]}"; do
            [[ -f "$root/$design/$file" ]] || return 1
        done
    done
}

find_family_root() {
    local extraction_root=$1
    local marker=$2
    local marker_path
    marker_path=$(find "$extraction_root" -type f \
        -path "*/adaptec1/$marker" -print -quit)
    [[ -n $marker_path ]] || return 1
    dirname "$(dirname "$marker_path")"
}

install_family() {
    local source=$1
    local destination=$2
    mkdir -p "$destination"
    cp -a "$source/." "$destination/"
}

download_ispd2005() {
    local destination=$1
    local work=$2
    local archive="$work/ispd2005dp.tar.xz"
    local extraction="$work/ispd2005-extract"
    local source

    echo "Downloading ISPD 2005..."
    mkdir -p "$extraction"
    curl --fail --location --retry 3 --output "$archive" \
        "${ISPD2005_URL:-$ISPD2005_DEFAULT_URL}"
    tar -xJf "$archive" -C "$extraction"
    source=$(find_family_root "$extraction" "adaptec1.dp.aux") || \
        die "ISPD 2005 archive does not contain the expected benchmarks"
    install_family "$source" "$destination"
}

download_ispd2015() {
    local destination=$1
    local work=$2
    local archive="$work/ispd2015dp.tar.xz"
    local extraction="$work/ispd2015-extract"
    local marker_path source

    echo "Downloading ISPD 2015..."
    mkdir -p "$extraction"
    curl --fail --location --retry 3 --output "$archive" \
        "${ISPD2015_URL:-$ISPD2015_DEFAULT_URL}"
    tar -xJf "$archive" -C "$extraction"
    marker_path=$(find "$extraction" -type f \
        -path '*/mgc_fft_1/floorplan.def' -print -quit)
    [[ -n $marker_path ]] || \
        die "ISPD 2015 archive does not contain the expected benchmarks"
    source=$(dirname "$(dirname "$marker_path")")
    install_family "$source" "$destination"
}

download_ispd2005free() {
    local destination=$1
    local work=$2
    local archive="$work/ispd2005free.zip"
    local extraction="$work/ispd2005free-extract"
    local nested_extraction="$work/ispd2005free-nested"
    local nested_archive
    local source=""

    echo "Downloading ISPD 2005 free-macro benchmarks..."
    mkdir -p "$extraction"
    curl --fail --location --retry 3 --output "$archive" \
        "${ISPD2005FREE_URL:-$ISPD2005FREE_DEFAULT_URL}"
    unzip -q "$archive" -d "$extraction"

    source=$(find_family_root "$extraction" "adaptec1_allfree.aux" || true)
    if [[ -z $source ]]; then
        nested_archive=$(find "$extraction" -type f -name '*.zip' -print -quit)
        if [[ -n $nested_archive ]]; then
            mkdir -p "$nested_extraction"
            unzip -q "$nested_archive" -d "$nested_extraction"
            source=$(find_family_root "$nested_extraction" \
                "adaptec1_allfree.aux" || true)
        fi
    fi
    [[ -n $source ]] || \
        die "ISPD 2005 free archive does not contain the expected benchmarks"
    install_family "$source" "$destination"
}

resolve_executable() {
    local requested=$1
    local label=$2
    local resolved

    if [[ $requested == */* ]]; then
        [[ -x $requested ]] || die "$label is not executable: $requested"
        resolved=$(cd "$(dirname "$requested")" && pwd)/$(basename "$requested")
    else
        resolved=$(command -v "$requested" 2>/dev/null || true)
        [[ -n $resolved ]] || die "$label command not found: $requested"
    fi
    echo "$resolved"
}

write_dreamplace_config() {
    local source=$1
    local destination=$2
    local aux_input=$3
    local result_dir=$4

    python3 - "$source" "$destination" "$aux_input" "$result_dir" <<'PY'
import json
import sys

source, destination, aux_input, result_dir = sys.argv[1:]
with open(source, encoding="utf-8") as input_file:
    config = json.load(input_file)
if not isinstance(config, dict):
    raise SystemExit(f"configuration must contain a JSON object: {source}")
config["aux_input"] = aux_input
config["result_dir"] = result_dir
with open(destination, "w", encoding="utf-8") as output:
    json.dump(config, output, indent=2)
    output.write("\n")
PY
}

validate_dreamplace_configs() {
    local config_dir=$1
    shift
    local -a designs=("$@")
    local design

    for design in "${designs[@]}"; do
        [[ -f "$config_dir/ispd2005/$design.json" ]] || \
            die "DREAMPlace configuration not found: $config_dir/ispd2005/$design.json"
        [[ -f "$config_dir/ispd2005free/${design}_allfree.json" ]] || \
            die "DREAMPlace configuration not found: $config_dir/ispd2005free/${design}_allfree.json"
    done
}

run_dreamplace() {
    local repo_root=$1
    local work=$2
    local config_dir=$3
    shift 3
    local -a designs=("$@")
    local family design design_name aux_input source_config config result_dir
    local generated destination

    for family in ispd2005 ispd2005free; do
        for design in "${designs[@]}"; do
            if [[ $family == ispd2005free ]]; then
                design_name="${design}_allfree"
                aux_input="$repo_root/data/$family/$design/${design_name}.aux"
                destination="$repo_root/data/ispd2005free-dreamplace/${design_name}.gp.pl"
            else
                design_name=$design
                aux_input="$repo_root/data/$family/$design/$design.aux"
                destination="$repo_root/data/ispd2005-dreamplace/$design.gp.pl"
            fi

            result_dir="$work/results/$family"
            config="$work/${family}-${design}.json"
            source_config="$config_dir/$family/${design_name}.json"
            [[ -f $source_config ]] || \
                die "DREAMPlace configuration not found: $source_config"
            write_dreamplace_config "$source_config" "$config" "$aux_input" \
                "$result_dir"
            echo "Running DREAMPlace for $family/$design..."
            (cd "$work" && "${DREAMPLACE_COMMAND[@]}" "$config")

            generated="$result_dir/$design_name/${design_name}.gp.pl"
            [[ -f $generated ]] || \
                die "DREAMPlace did not create the expected file: $generated"
            mkdir -p "$(dirname "$destination")"
            cp "$generated" "$destination.tmp"
            mv "$destination.tmp" "$destination"
            echo "Wrote ${destination#"$repo_root/"}"
        done
    done
}

dreamplace=""
placer=""
python=""
config_dir=""
declare -a selected_designs=()

while (($#)); do
    case $1 in
        --dreamplace)
            (($# >= 2)) || die "--dreamplace requires an executable"
            dreamplace=$2
            shift 2
            ;;
        --placer)
            (($# >= 2)) || die "--placer requires a path"
            placer=$2
            shift 2
            ;;
        --python)
            (($# >= 2)) || die "--python requires an interpreter"
            python=$2
            shift 2
            ;;
        --config-dir)
            (($# >= 2)) || die "--config-dir requires a directory"
            config_dir=$2
            shift 2
            ;;
        --design)
            (($# >= 2)) || die "--design requires a benchmark name"
            is_known_design "$2" || die "unknown design: $2"
            selected_designs+=("$2")
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

if ((${#selected_designs[@]} == 0)); then
    selected_designs=("${ISPD2005_DESIGNS[@]}")
fi

require_command curl
require_command tar
require_command unzip

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/placement-data.XXXXXX")
trap 'rm -rf "$work"' EXIT

if ispd2005_family_is_complete "$repo_root/data/ispd2005" ".dp.aux"; then
    echo "ISPD 2005 benchmarks are already present."
else
    download_ispd2005 "$repo_root/data/ispd2005" "$work"
fi

if ispd2005_family_is_complete "$repo_root/data/ispd2005free" "_allfree.aux"; then
    echo "ISPD 2005 free-macro benchmarks are already present."
else
    download_ispd2005free "$repo_root/data/ispd2005free" "$work"
fi

if ispd2015_is_complete "$repo_root/data/ispd2015"; then
    echo "ISPD 2015 benchmarks are already present."
else
    download_ispd2015 "$repo_root/data/ispd2015" "$work"
fi

ispd2005_family_is_complete "$repo_root/data/ispd2005" ".dp.aux" || \
    die "ISPD 2005 installation is incomplete"
ispd2005_family_is_complete "$repo_root/data/ispd2005free" "_allfree.aux" || \
    die "ISPD 2005 free-macro installation is incomplete"
ispd2015_is_complete "$repo_root/data/ispd2015" || \
    die "ISPD 2015 installation is incomplete"

if [[ -z $dreamplace && -z $placer && -z $python && -z $config_dir ]]; then
    echo "Benchmarks are ready. DREAMPlace was not requested."
    exit 0
fi

require_command python3
declare -a DREAMPLACE_COMMAND
[[ -n $config_dir ]] || die "--config-dir is required to run DREAMPlace"
[[ -d $config_dir ]] || die "DREAMPlace configuration directory not found: $config_dir"
config_dir=$(cd "$config_dir" && pwd)

if [[ -n $dreamplace ]]; then
    [[ -z $placer && -z $python ]] || \
        die "--dreamplace cannot be combined with --placer or --python"
    dreamplace=$(resolve_executable "$dreamplace" "DREAMPlace wrapper")
    DREAMPLACE_COMMAND=("$dreamplace")
else
    [[ -n $placer ]] || die "--placer or --dreamplace is required"
    [[ -n $python ]] || die "--python is required when --placer is used"
    [[ -f $placer ]] || die "DREAMPlace launcher not found: $placer"
    placer=$(cd "$(dirname "$placer")" && pwd)/$(basename "$placer")
    python=$(resolve_executable "$python" "Python interpreter")
    DREAMPLACE_COMMAND=("$python" "$placer")
fi

validate_dreamplace_configs "$config_dir" "${selected_designs[@]}"
run_dreamplace "$repo_root" "$work" "$config_dir" "${selected_designs[@]}"
echo "Benchmarks and requested DREAMPlace solutions are ready."
