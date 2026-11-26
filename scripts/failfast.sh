#!/bin/bash
# Runs make with the given arguments and stops the whole build at the first error. The output also goes to
# build/log/make-GOALS.log (like make-cross.log), and the package builds that failed are printed last.
set -uo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
state=$top/build/state
started=$EPOCHSECONDS

goals=()
for a in "$@"; do
    case $a in -* | *=*) ;; *) goals+=("$a") ;; esac
done
[ ${#goals[@]} -gt 0 ] || goals=(all)
name=$(IFS=-; echo "${goals[*]}")
name=${name//\//_}
mkdir -p "$top/build/log"
log=$top/build/log/make-${name:0:100}.log
exec 4>"$log"

out() {
    printf '%s\n' "$1"
    printf '%s\n' "$1" >&4
}

# The package builds that failed during this run, from build/state (written by scripts/buildpackage.sh --name)
failed_builds() {
    local f k v st en reason flog
    for f in "$state"/*; do
        [ -f "$f" ] && [[ $f != *.tmp ]] || continue
        st= en=0 reason= flog=
        while IFS== read -r k v; do
            case $k in
            state) st=$v ;;
            end) en=$v ;;
            reason) reason=$v ;;
            log) flog=$v ;;
            esac
        done <"$f"
        if [ "$st" = failed ] && [ "${en%%.*}" -ge "$started" ]; then
            out "failfast.sh: FAILED ${f##*/}: $reason  (see $flog)"
        fi
    done
}

# make's error, or ninja's (which would otherwise let the builds that run finish first)
error='^(make(\[[0-9]+\])?: \*\*\* .*Error [0-9]+|FAILED: )'

exec 3< <(exec setsid make "$@" 2>&1)
pid=$!
failed=0
while IFS= read -r line <&3 || [ -n "$line" ]; do
    out "$line"
    if [[ $line =~ $error ]]; then
        failed=1
        out "failfast.sh: stopping the build at the first error"
        kill -TERM -- "-$pid" 2>/dev/null
        sleep 5
        kill -KILL -- "-$pid" 2>/dev/null
        break
    fi
done
exec 3<&-
wait "$pid"
rc=$?
# The failed builds come last, so that they cannot be missed
if [ "$failed" = 1 ] || [ "$rc" != 0 ]; then
    failed_builds
fi
[ "$failed" = 1 ] && exit 2
exit "$rc"
