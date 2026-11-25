#!/bin/bash
# Removes firmware from a root file system that no kernel module in it asks for: whole directories whose driver is
# not built, i915 firmware that the i915 module does not list, and older iwlwifi firmware versions
set -euo pipefail
shopt -s nullglob globstar

root=${1:?usage: fwprune.sh ROOT}
fw=$root/usr/lib/firmware
kernel=("$root"/usr/lib/modules/*/kernel)

declare -A owners=(
    [amdgpu]="amdgpu"
    [xe]="xe"
    [cirrus]="snd-hda-scodec-cs35l41* snd-hda-scodec-cs35l56*"
    ["intel/sof*"]="snd-sof"
    [ath10k]="ath10k_core"
    [ath11k]="ath11k"
    [ath12k]="ath12k"
    [rtw89]="rtw89_core"
    [mediatek/mt7996]="mt7996e"
    ["mediatek/WIFI_MT7925*"]="mt7925-common"
    ["mediatek/WIFI_MT7922*"]="mt7921-common"
    ["mediatek/WIFI_MT7961*"]="mt7921-common"
)

size() {
    if [ -L "$1" ]; then echo 0; else stat -c %s "$1"; fi
}

for d in "${!owners[@]}"; do
    found=0
    for n in ${owners[$d]}; do
        for k in "${kernel[@]}"; do
            for m in "$k"/**/$n.ko*; do
                found=1
                break 3
            done
        done
    done
    [ "$found" = 1 ] && continue
    for path in "$fw"/$d; do
        rm -rf "$path"
        echo "fwprune.sh: removed ${path#"$root"/}, since no module in this image uses it"
    done
done

i915dir=$fw/i915
i915mods=()
for k in "${kernel[@]}"; do i915mods+=("$k"/drivers/gpu/drm/i915/i915.ko*); done
if [ -d "$i915dir" ] && [ ${#i915mods[@]} -gt 0 ]; then
    declare -A keep=()
    for mod in "${i915mods[@]}"; do
        while read -r n; do
            case $n in i915/*) keep[${n#i915/}]=1 ;; esac
        done < <(modinfo -F firmware "$mod")
    done
    declare -A keepfile=()
    for path in "$i915dir"/*; do
        f=${path##*/}
        if [ -n "${keep[${f%.zst}]:-}" ]; then
            keepfile[$f]=1
            [ -L "$path" ] && t=$(realpath "$path") && keepfile[${t##*/}]=1
        fi
    done
    freed=0
    for path in "$i915dir"/*; do
        if [ -z "${keepfile[${path##*/}]:-}" ]; then
            freed=$((freed + $(size "$path")))
            rm -f "$path"
        fi
    done
    echo "fwprune.sh: removed $((freed >> 20)) MiB of i915 firmware that the i915 module does not ask for"
fi

fwdir=$fw/intel/iwlwifi
mods=()
for k in "${kernel[@]}"; do mods+=("$k"/drivers/net/wireless/intel/iwlwifi/iwlwifi.ko*); done
[ -d "$fwdir" ] && [ ${#mods[@]} -gt 0 ] || exit 0
for k in "${kernel[@]}"; do mods+=("$k"/drivers/net/wireless/intel/iwl*/**/*.ko*); done
mapfile -t mods < <(printf '%s\n' "${mods[@]}" | sort -u)

# NAME-[c]VERSION.ucode[.EXT]: the chip is NAME and the c flag, and each module asks for its newest VERSION
ucode='^(.*)-(c?)([0-9]+)\.ucode(\.[a-z]+)?$'
declare -A wanted=() family=()
for mod in "${mods[@]}"; do
    while read -r n; do
        if [[ $n =~ $ucode ]]; then
            chip=${BASH_REMATCH[1]}:${BASH_REMATCH[2]}
            v=$((10#${BASH_REMATCH[3]}))
            [ "$v" -gt "${wanted[$chip]:--1}" ] && wanted[$chip]=$v
        fi
    done < <(modinfo -F firmware "$mod")
done
for chip in "${!wanted[@]}"; do
    name=${chip%:*}
    IFS=- read -r a b _ <<<"$name"
    fam=$a${b:+-$b}:${chip##*:}
    [ "${wanted[$chip]}" -gt "${family[$fam]:--1}" ] && family[$fam]=${wanted[$chip]}
done

declare -A files=()
for path in "$fwdir"/*; do
    f=${path##*/}
    [ -L "$path" ] && continue
    [[ $f =~ $ucode ]] || continue
    files[${BASH_REMATCH[1]}:${BASH_REMATCH[2]}]+="$((10#${BASH_REMATCH[3]})):$f "
done

freed=0
for chip in "${!files[@]}"; do
    name=${chip%:*}
    IFS=- read -r a b _ <<<"$name"
    cap=${wanted[$chip]:-${family[$a${b:+-$b}:${chip##*:}]:-}}
    best=
    if [ -n "$cap" ]; then
        best=$(printf '%s\n' ${files[$chip]} | awk -F: -v cap="$cap" '$1 <= cap' | sort -t: -k1,1n -k2,2 | tail -n 1)
        [ -n "$best" ] || continue
    fi
    for e in ${files[$chip]}; do
        [ "$e" = "$best" ] && continue
        freed=$((freed + $(size "$fwdir/${e#*:}")))
        rm -f "$fwdir/${e#*:}"
    done
done

for path in "$fwdir"/*; do
    f=${path##*/}
    [[ $f =~ ^(.*)\.pnvm(\.[a-z]+)?$ ]] || continue
    stem=${BASH_REMATCH[1]}
    used=0
    for chip in "${!wanted[@]}"; do
        case ${chip%:*} in "$stem"*) used=1 && break ;; esac
    done
    if [ "$used" = 0 ]; then
        freed=$((freed + $(size "$path")))
        rm -f "$path"
    fi
done

for path in "$fwdir"/* "$fw"/iwlwifi-*; do
    [ -L "$path" ] && [ ! -e "$path" ] && rm -f "$path"
done
echo "fwprune.sh: removed $((freed >> 20)) MiB of older iwlwifi firmware"
