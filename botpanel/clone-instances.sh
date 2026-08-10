#!/usr/bin/env bash
#
# clone-instances.sh - duplicate a bot instance into user_instances/bN
#
# install-instances downloads b0. This clones it into b1, b2, ... so you do not
# have to re-download or hand-copy for every bot.
#
# Usage:
#   ./clone-instances.sh 5                 # add 5 instances, lowest free indices
#   ./clone-instances.sh -s b0 -f 3        # from b0, overwriting if they exist
#   ./clone-instances.sh -t b7 -t b9       # specific names instead of a count
#   ./clone-instances.sh -n 4              # dry run
# Made by HappyKuro

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INSTANCES_DIR="$SCRIPT_DIR/user_instances"

SOURCE="b0"
FORCE=0
DRY_RUN=0
COUNT=""
TARGETS=()

die() { printf 'error: %s\n' "$1" >&2; exit 1; }
info() { printf '%s\n' "$1"; }

usage() {
    sed -n '3,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Options:
  -s, --source NAME   template instance to copy (default: b0)
  -t, --target NAME   explicit target name; repeatable. Overrides <count>.
  -f, --force         replace a target that already exists
  -n, --dry-run       print what would happen, change nothing
  -h, --help          this text
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -s|--source) SOURCE="${2:-}"; [[ -n "$SOURCE" ]] || die "--source needs a value"; shift 2 ;;
        -t|--target) [[ -n "${2:-}" ]] || die "--target needs a value"; TARGETS+=("$2"); shift 2 ;;
        -f|--force)  FORCE=1; shift ;;
        -n|--dry-run) DRY_RUN=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        -*)          die "unknown option: $1" ;;
        *)
            [[ -z "$COUNT" ]] || die "give a count once, or use --target"
            [[ "$1" =~ ^[0-9]+$ ]] || die "count must be a number, got: $1"
            COUNT="$1"; shift ;;
    esac
done

[[ -d "$INSTANCES_DIR" ]] || die "no user_instances/ next to this script - run install-instances first"

SOURCE_PATH="$INSTANCES_DIR/$SOURCE"
[[ -d "$SOURCE_PATH" ]] || die "template '$SOURCE' not found in $INSTANCES_DIR"

# Work out the targets.
if [[ ${#TARGETS[@]} -eq 0 ]]; then
    [[ -n "$COUNT" ]] || { usage; exit 1; }
    [[ "$COUNT" -gt 0 ]] || die "count must be at least 1"

    # Lowest free b-indices, so gaps left by a deleted instance get reused
    # instead of the numbering creeping upward forever.
    index=0
    while [[ ${#TARGETS[@]} -lt "$COUNT" ]]; do
        candidate="b$index"
        if [[ ! -e "$INSTANCES_DIR/$candidate" ]]; then
            TARGETS+=("$candidate")
        fi
        index=$((index + 1))
        [[ "$index" -lt 10000 ]] || die "ran out of indices below 10000"
    done
fi

# Refuse the whole run rather than half of it if any target is in the way.
for target in "${TARGETS[@]}"; do
    [[ "$target" != "$SOURCE" ]] || die "target '$target' is the template itself"
    if [[ -e "$INSTANCES_DIR/$target" && "$FORCE" -ne 1 ]]; then
        die "'$target' already exists - pass --force to replace it"
    fi
done

# A Steam instance is several GB; running out of disk halfway through leaves a
# corrupt instance that looks complete. Check before copying anything.
source_kb="$(du -sk "$SOURCE_PATH" 2>/dev/null | cut -f1)"
free_kb="$(df -Pk "$INSTANCES_DIR" | awk 'NR==2 {print $4}')"
needed_kb=$((source_kb * ${#TARGETS[@]}))

human() { awk -v k="$1" 'BEGIN { printf "%.1f GiB", k/1048576 }'; }

info "template : $SOURCE ($(human "$source_kb"))"
info "targets  : ${TARGETS[*]}"
info "needs    : $(human "$needed_kb"), free: $(human "$free_kb")"

if [[ "$needed_kb" -ge "$free_kb" ]]; then
    die "not enough free space on the filesystem holding user_instances/"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
    info "dry run - nothing copied"
    exit 0
fi

for target in "${TARGETS[@]}"; do
    target_path="$INSTANCES_DIR/$target"

    if [[ -e "$target_path" ]]; then
        info "removing existing $target"
        rm -rf -- "$target_path"
    fi

    info "creating $target"

    # cp -a, not cp -r: the template holds symlinks (.steampath, .steampid) that
    # must stay links rather than become copies of what they point at, and
    # .Xauthority / .pulse-cookie are 0600 and must stay that way. Copy into a
    # .partial name first so an interrupted run cannot leave something that looks
    # like a finished instance.
    partial="$target_path.partial"
    rm -rf -- "$partial"
    cp -a -- "$SOURCE_PATH" "$partial"
    mv -- "$partial" "$target_path"

    chmod -R u+rwX -- "$target_path"
done

info "done - ${#TARGETS[@]} instance(s) created in $INSTANCES_DIR"
info "note: every clone carries the template's Steam login. Log each one into its"
info "      own account before running them together."
