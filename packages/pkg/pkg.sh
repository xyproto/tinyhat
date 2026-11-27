#!/bin/sh
pm=/usr/lib/pacman/pacman
al=/usr/bin/tinyhat-alpine
tb=/usr/lib/tinyhat/pacman-tinybuild

alpine() {
	[ -x "$al" ] && [ -d /usr/lib/alpine/etc/apk ] || return 1
	case $(uname -m) in
	i?86) grep -qw sse2 /proc/cpuinfo ;;
	esac
}

no_alpine() {
	case $(uname -m) in
	i?86) grep -qw sse2 /proc/cpuinfo || { echo "$1: Alpine Linux packages for 32-bit x86 need a CPU with SSE2, and this CPU has none" >&2; exit 1; } ;;
	esac
	echo "$1: the Alpine Linux root in /usr/lib/alpine is missing" >&2
	exit 1
}

alpine_index() {
	ls /usr/lib/alpine/var/cache/apk/APKINDEX.* >/dev/null 2>&1 || "$al" --apk update >/dev/null
}

alpine_has() {
	alpine || return 1
	"$al" apk search --exact "$1" 2>/dev/null | grep -q .
}

alpine_installed() {
	alpine || return 1
	"$al" apk info -e "$1" >/dev/null 2>&1 && ! $pm -Q "$1" >/dev/null 2>&1
}

alpine_world() {
	grep -v -e '^!' -e '^$' /usr/lib/alpine/etc/apk/world | sed 's/[<>=~].*//' | while read -r a; do
		case $a in alpine-baselayout | alpine-keys | alpine-release | apk-tools | ca-certificates-bundle | tinyhat-provides) continue ;; esac
		echo "$a"
	done
}

tinybuild() {
	[ -d "/var/lib/tinyhat/tinybuilds/$1" ] || [ -d "/usr/share/tinyhat/tinybuilds/$1" ]
}

ours() {
	$pm -Q "$1" >/dev/null 2>&1 && return 0
	case " $(pacman-conf IgnorePkg 2>/dev/null | tr '\n' ' ') " in *" $1 "*) return 0 ;; esac
	return 1
}

asroot() {
	if [ "$(id -u)" = 0 ]; then
		"$@"
	else
		doas "$@"
	fi
}

usage() {
	cat <<'EOF'
pkg install PACKAGE...   install packages, or package files
                         (alpine/PACKAGE installs from Alpine Linux, arch/PACKAGE from Arch Linux)
pkg remove PACKAGE...    remove packages and the dependencies nothing else needs
pkg update               download the latest package lists
pkg upgrade              upgrade the installed packages
pkg sourceupgrade        rebuild the installed TINYBUILD packages that have a newer version
pkg search WORD...       search all packages: TINYBUILDs, Tiny Hat, Arch Linux and Alpine Linux
pkg info PACKAGE         show information about a package
pkg list PACKAGE         list the files in an installed package
pkg listinstalled        list the installed packages

Packages come from Tiny Hat first, then from the Arch Linux repositories (archlinux32 on i686),
and from Alpine Linux (in /usr/lib/alpine) only when there is no installable Arch Linux package.
Packages with a TINYBUILD or PKGBUILD in /usr/share/tinyhat/tinybuilds are built from source and
then installed. Alpine programs run directly, or with tinyhat-alpine.

apk is pkg too: "apk add", "apk del" and the other apk commands work on the same packages.
pacman installs only Tiny Hat, Arch Linux and TINYBUILD packages, never Alpine packages.
See pacman -h and apk --help.
EOF
}

pkg_install() {
	files=
	names=
	for a in "$@"; do
		if [ -f "$a" ]; then
			files="$files $(realpath -- "$a")"
		else
			names="$names $a"
		fi
	done
	if [ -n "$files" ]; then
		asroot /usr/bin/pacman -U --noconfirm --needed $files || exit 1
	fi
	[ -n "$names" ] || exit 0
	asroot /usr/bin/pkg sync-with-alpine -S --noconfirm --needed $names
	exit
}

pkg_remove() {
	[ "$(id -u)" = 0 ] || exec doas /usr/bin/pkg remove "$@"
	apk=
	rest=
	for a in "$@"; do
		case $a in
		alpine/*) apk="$apk ${a#alpine/}" ;;
		arch/*) rest="$rest ${a#arch/}" ;;
		*)
			if alpine_installed "$a"; then
				apk="$apk $a"
			else
				rest="$rest $a"
			fi
			;;
		esac
	done
	rc=0
	[ -z "$rest" ] || /usr/bin/pacman -Rs $rest || rc=1
	[ -z "$apk" ] || "$al" --apk del $apk || rc=1
	exit $rc
}

matching() {
	awk -F '\t' -v words="$*" '
	BEGIN { n = split(tolower(words), w, " ") }
	{
		s = tolower($2 " " $4)
		for (i = 1; i <= n; i++)
			if (!index(s, w[i]))
				next
		print
	}'
}

show() {
	awk -F '\t' -v words="$*" '
	BEGIN { n = split(tolower(words), w, " ") }
	!seen[$1 "/" $2]++ {
		name = tolower($2)
		r = 2
		if (n == 1 && name == w[1])
			r = 0
		else {
			r = 1
			for (i = 1; i <= n; i++)
				if (!index(name, w[i]))
					r = 2
		}
		out[r] = out[r] sprintf("%s/%s %s%s\n", $1, $2, $3, ($5 == "" ? "" : " [" $5 "]"))
		if ($4 != "")
			out[r] = out[r] sprintf("    %s\n", $4)
	}
	END { printf "%s%s%s", out[0], out[1], out[2] }'
}

field() {
	sed -n -e "s/^set $1 //p" -e "s/^$1=//p" "$2" | head -n 1 | sed -e "s/^['\"]//" -e "s/['\"]\$//"
}

search_tinybuilds() {
	for d in /var/lib/tinyhat/tinybuilds/*/ /usr/share/tinyhat/tinybuilds/*/; do
		f=${d}TINYBUILD
		[ -f "$f" ] || f=${d}PKGBUILD
		[ -f "$f" ] || continue
		name=${d%/}
		name=${name##*/}
		have=$($pm -Q "$name" 2>/dev/null | cut -d' ' -f2)
		printf 'tinybuild\t%s\t%s-%s\t%s\t%s\n' "$name" "$(field pkgver "$f")" "$(field pkgrel "$f")" \
			"$(field pkgdesc "$f")" "${have:+installed: $have}"
	done | awk -F '\t' '!seen[$2]++' | matching "$@"
}

search_tinyhat() {
	{
		$pm -Slq 2>/dev/null
		for d in /var/lib/tinyhat/tinybuilds/*/ /usr/share/tinyhat/tinybuilds/*/; do
			d=${d%/}
			echo "${d##*/}"
		done
		echo
		$pm -Qi 2>/dev/null
	} | awk '
	!local && $0 == "" { local = 1; next }
	!local { skip[$0] = 1; next }
	/^Name +:/ { name = $3; next }
	/^Version +:/ { ver = $3; next }
	/^Description +:/ { sub(/^Description +: /, ""); desc = $0; next }
	/^$/ { if (name != "" && !skip[name]) printf "tinyhat\t%s\t%s\t%s\tinstalled\n", name, ver, desc; name = "" }
	' | matching "$@"
}

search_arch() {
	$pm -Ss "$@" 2>/dev/null | awk '
	/^[^ ]/ {
		if (line != "") print line "\t" desc "\t" mark
		split($1, rn, "/")
		mark = ""
		if (match($0, /\[installed[^]]*\]/))
			mark = substr($0, RSTART + 1, RLENGTH - 2)
		line = rn[1] "\t" rn[2] "\t" $2
		desc = ""
		next
	}
	{ sub(/^ +/, ""); desc = $0 }
	END { if (line != "") print line "\t" desc "\t" mark }
	'
}

search_alpine() {
	alpine || return 0
	if ! ls /usr/lib/alpine/var/cache/apk/APKINDEX.* >/dev/null 2>&1; then
		echo "pkg: the Alpine package list is missing, run pkg update" >&2
		return 0
	fi
	{
		"$al" apk info -v 2>/dev/null
		echo
		"$al" apk search -v --description "$1" 2>/dev/null
	} | awk -v first="$1" '
	!list && $0 == "" { list = 1; next }
	!list { inst[$0] = 1; next }
	{
		i = index($0, " - ")
		nv = i ? substr($0, 1, i - 1) : $0
		desc = i ? substr($0, i + 3) : ""
		if (!match(nv, /-[0-9][^-]*-r[0-9]+$/))
			next
		name = substr(nv, 1, RSTART - 1)
		if (name != first && name ~ /-(doc|dbg|lang|openrc|bash-completion|zsh-completion|fish-completion)$/)
			next
		printf "alpine\t%s\t%s\t%s\t%s\n", name, substr(nv, RSTART + 1), desc, inst[nv] ? "installed" : ""
	}
	' | matching "$@"
}

pkg_search() {
	[ $# -gt 0 ] || { echo "usage: pkg search WORD..." >&2; exit 1; }
	{
		search_tinybuilds "$@"
		search_tinyhat "$@"
		search_arch "$@"
		search_alpine "$@"
	} | show "$@"
	exit 0
}

pkg_info() {
	[ $# -gt 0 ] || exec $pm -Qi
	for a in "$@"; do
		case $a in
		alpine/*)
			"$al" apk info -a "${a#alpine/}"
			continue
			;;
		arch/*)
			$pm -Si "${a#arch/}"
			continue
			;;
		esac
		if $pm -Q "$a" >/dev/null 2>&1; then
			$pm -Qi "$a"
		elif alpine_installed "$a"; then
			"$al" apk info -a "$a"
		elif $pm -Si "$a" >/dev/null 2>&1; then
			$pm -Si "$a"
		elif alpine_has "$a"; then
			"$al" apk info -a "$a"
		else
			echo "error: package '$a' was not found" >&2
		fi
	done
	exit 0
}

pkg_list() {
	[ $# -gt 0 ] || exec $pm -Ql
	for a in "$@"; do
		if alpine_installed "$a"; then
			"$al" apk info -L "$a"
		else
			$pm -Ql "$a"
		fi
	done
	exit
}

pkg_listinstalled() {
	$pm -Q "$@"
	if alpine && [ $# = 0 ]; then
		for a in $(alpine_world); do
			"$al" apk info -e -v "$a" 2>/dev/null | sed 's|^|alpine/|'
		done
	fi
	exit 0
}

pkg_main() {
	cmd=$1
	[ $# = 0 ] || shift
	case $cmd in
	install) pkg_install "$@" ;;
	remove) pkg_remove "$@" ;;
	update) asroot /usr/bin/pkg sync-with-alpine -Sy "$@"; exit ;;
	sync-with-alpine) withalpine=1 ;;
	upgrade)
		asroot /usr/bin/pacman -Syu --noconfirm "$@" || exit 1
		alpine && { asroot "$al" --apk upgrade --update-cache || exit 1; }
		exit 0
		;;
	sourceupgrade) asroot "$tb" --upgrade "$@"; exit ;;
	search) pkg_search "$@" ;;
	info) pkg_info "$@" ;;
	list) pkg_list "$@" ;;
	listinstalled) pkg_listinstalled "$@" ;;
	"" | help | -h | --help) usage; exit 0 ;;
	-*) exec /usr/bin/pacman "$cmd" "$@" ;;
	*) echo "pkg: unknown command: $cmd (see pkg help)" >&2; exit 1 ;;
	esac
}

apk_cli() {
	cmd=$1
	[ $# = 0 ] || shift
	names=
	refresh=
	for a in "$@"; do
		case $a in
		-U | --update-cache) refresh=1 ;;
		-*) ;;
		*) names="$names $a" ;;
		esac
	done
	case $cmd in
	add)
		[ -z "$refresh" ] || asroot /usr/bin/pkg sync-with-alpine -Sy || exit 1
		pkg_install $names
		;;
	del) pkg_remove $names ;;
	update) pkg update ;;
	upgrade) pkg upgrade ;;
	search) pkg_search $names ;;
	info)
		case " $* " in
		*" -L "* | *" --contents "*) pkg_list $names ;;
		*" -e "* | *" --installed "*)
			for a in $names; do
				$pm -Q "$a" >/dev/null 2>&1 || alpine_installed "$a" || exit 1
			done
			exit 0
			;;
		esac
		[ -n "$names" ] || pkg_listinstalled
		pkg_info $names
		;;
	"" | help | -h | --help)
		cat <<'EOF'
apk add PACKAGE...    install packages (pkg install)
apk del PACKAGE...    remove packages (pkg remove)
apk update            download the latest package lists (pkg update)
apk upgrade           upgrade the installed packages (pkg upgrade)
apk search WORD       search for packages (pkg search)
apk info [PACKAGE]    show information about a package, or list the installed packages
apk info -L PACKAGE   list the files in an installed package

These work on all packages: Tiny Hat, Arch Linux and Alpine Linux. Other apk commands go to the
apk of the Alpine Linux in /usr/lib/alpine. See pkg help.
EOF
		exit 0
		;;
	*)
		alpine || no_alpine apk
		if [ "$(id -u)" = 0 ]; then
			exec "$al" --apk "$cmd" "$@"
		fi
		exec "$al" apk "$cmd" "$@"
		;;
	esac
}

withalpine=
case ${0##*/} in
apk) apk_cli "$@" ;;
pacman) ;;
*)
	pkg_main "$@"
	shift
	;;
esac

case $1 in
install | remove | update | upgrade | sourceupgrade | search | info | list | listinstalled)
	echo "pacman: \"pacman $1\" is \"pkg $1\"" >&2
	exit 1
	;;
esac
if [ -x /usr/lib/tinyhat/pacman-first-use ] && [ "$(id -u)" = 0 ]; then
	/usr/lib/tinyhat/pacman-first-use "$@"
fi
case $1 in
-S* | -U* | --sync | --upgrade) ;;
*) exec $pm "$@" ;;
esac
if [ "$(id -u)" != 0 ] || [ ! -x /usr/lib/tinyhat/pacman-compat ]; then
	exec $pm "$@"
fi
tinybuilds=
apks=
noconfirm=
needed=
case $1 in
-S | -Sy | -Syy | -Su | -Syu | -Suy | --sync)
	op=$1
	shift
	keep=
	rest=
	optarg=
	refresh=
	case $op in
	-*yy*) refresh=-Syy ;;
	-[!-]*y*) refresh=-Sy ;;
	*) ls "$(pacman-conf DBPath 2>/dev/null || echo /var/lib/pacman/)sync"/*.db >/dev/null 2>&1 || refresh=-Sy ;;
	esac
	if [ -n "$refresh" ]; then
		$pm $refresh || exit 1
		[ -n "$withalpine" ] && alpine && "$al" --apk update >/dev/null
		op=$(echo "$op" | tr -d y)
		[ "$op" = - ] && op=-S
	fi
	for a in "$@"; do
		if [ -n "$optarg" ]; then
			keep="$keep $a"
			optarg=
			continue
		fi
		case $a in
		--noconfirm) noconfirm=$a; keep="$keep $a" ;;
		--needed) needed=$a; keep="$keep $a" ;;
		--ask | --assume-installed | --arch | --cachedir | --color | --config | --dbpath | --gpgdir | --hookdir | \
			--ignore | --ignoregroup | --logfile | --overwrite | --print-format | --root | --sysroot | -b | -r)
			keep="$keep $a"
			optarg=1
			;;
		-*) keep="$keep $a" ;;
		alpine/*)
			if [ -z "$withalpine" ]; then
				echo "pacman: $a is an Alpine package, install it with \"pkg install $a\"" >&2
				exit 1
			fi
			alpine || no_alpine pkg
			apks="$apks ${a#alpine/}"
			;;
		arch/*) rest="$rest ${a#arch/}" ;;
		*)
			if [ -x "$tb" ] && tinybuild "$a"; then
				tinybuilds="$tinybuilds $a"
			elif [ -z "$withalpine" ] || ours "$a" || ! alpine; then
				rest="$rest $a"
			elif $pm -Si "$a" >/dev/null 2>&1; then
				if $pm -Sp "$a" >/dev/null 2>&1; then
					rest="$rest $a"
				else
					alpine_index
					if alpine_has "$a"; then apks="$apks $a"; else rest="$rest $a"; fi
				fi
			else
				alpine_index
				if alpine_has "$a"; then apks="$apks $a"; else rest="$rest $a"; fi
			fi
			;;
		esac
	done
	set -- $op $keep $rest
	[ -z "$rest$tinybuilds$apks" ] && [ "$op" = -S ] && [ -n "$refresh" ] && exit 0
	if [ -z "$rest" ] && [ "$op" = -S ] && { [ -n "$tinybuilds" ] || [ -n "$apks" ]; }; then
		rc=0
		[ -z "$tinybuilds" ] || "$tb" $noconfirm $needed $tinybuilds || rc=1
		[ -z "$apks" ] || "$al" --apk add $apks || rc=1
		exit $rc
	fi
	;;
esac
before=$($pm -Qq 2>/dev/null)
$pm "$@"
rc=$?
new=$($pm -Qq 2>/dev/null | grep -vxF "$before")
[ -n "$new" ] && /usr/lib/tinyhat/pacman-compat --scan $new
if [ $rc -eq 0 ]; then
	[ -z "$tinybuilds" ] || "$tb" $noconfirm $needed $tinybuilds || rc=1
	[ -z "$apks" ] || "$al" --apk add $apks || rc=1
fi
exit $rc
