#!/bin/sh
# Builds the i686 packages that the given packages need at run time, from our TINYBUILDs, with the i686-cross
# toolchain in the x86_64 build environment (scripts/buildpackage.sh --cross), in the order buildplan.py
# cross-plan gives, stopping at the first failure. Package directories in a dependency cycle are built a second time,
# once everything they need exists. Logs are in build/log/cross-NAME.log.
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
cd "$top"
[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 2; }
plan=$(python3 scripts/buildplan.py cross-plan "$@")
mkdir -p build/log

build() {
  dir=$1
  shift
  name=${dir##*/}
  log=build/log/cross-$name.log
  printf '%s: ' "$name"
  if scripts/buildpackage.sh --cross "$dir" x86_64/i686-cross "$@" > "$log" 2>&1; then
    tail -n 1 "$log"
  else
    echo "failed, see $log"
    tail -n 30 "$log" >&2
    exit 1
  fi
}

# First pass: a package directory whose dependencies are not built yet gets only those that are
echo "$plan" | while read -r dir deps; do
  have=
  for d in $deps; do
    python3 scripts/buildplan.py pkgfile build/repo i686 "${d#i686/}" > /dev/null 2>&1 && have="$have $d"
  done
  build "$dir" $have
done
# Second pass: the same package directories with all their dependencies; those whose inputs did not change are cache hits
echo "$plan" | while read -r dir deps; do
  build "$dir" $deps
done
