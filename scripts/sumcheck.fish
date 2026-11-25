#!/usr/bin/env fish
set -l top (path resolve (status dirname)/..)
set -l dirs $argv
set -q dirs[1]; or set dirs $top/packages/*/
set -l bad 0
for d in $dirs
    test -f $d/TINYBUILD; or continue
    for line in (env CARCH=i686 fish -c 'source $argv[1]/TINYBUILD 2>/dev/null; for i in (seq (count $source)); printf "%s\t%s\t%s\t%s\n" $source[$i] "$sha256sums[$i]" "$sha512sums[$i]" "$b2sums[$i]"; end' $d 2>/dev/null)
        set -l f (string split \t -- $line)
        string match -q -- '*://*' $f[1]; and continue
        string match -q -- '*::*' $f[1]; and continue
        set -l file $d/$f[1]
        test -f $file; or continue
        set -l got
        set -l want
        if test -n "$f[2]" -a "$f[2]" != SKIP
            set want $f[2]
            set got (sha256sum $file | string split -f1 ' ')
        else if test -n "$f[3]" -a "$f[3]" != SKIP
            set want $f[3]
            set got (sha512sum $file | string split -f1 ' ')
        else if test -n "$f[4]" -a "$f[4]" != SKIP
            set want $f[4]
            set got (b2sum $file | string split -f1 ' ')
        else
            continue
        end
        if test "$got" != "$want"
            echo "sumcheck: "(string replace -- $top/ '' (path normalize $file))" does not match its checksum in "(string replace -- $top/ '' (path normalize $d/TINYBUILD)) >&2
            set bad 1
        end
    end
end
exit $bad
