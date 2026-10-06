bad=0
for f in /usr/bin/* /usr/lib/*.so*; do
	[ -L "$f" ] && continue
	case $f in /usr/lib/libthread_db.so.* | /usr/lib/libtss2-tcti-*) continue ;; esac
	[ "$(head -c4 "$f" 2>/dev/null | tail -c3)" = ELF ] || continue
	out=$(ldd -r "$f" 2>&1 | grep 'undefined symbol' | grep -v 'GLIBC_PRIVATE') || continue
	echo "$out" | sed "s|^|symcheck: $f: |"
	bad=1
done
exit $bad
