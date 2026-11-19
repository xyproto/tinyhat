function makep_elfcheck --on-event makep_check -a name dir
    contains -- !elfcheck $options; and return
    test "$pkgarch" = any; and return
    set -l class
    set -l machine
    set -l cpu
    switch $CARCH
        case i686
            set class ELF32
            set machine 'Intel 80386'
            set cpu i686
        case x32
            set class ELF32
            set machine 'Advanced Micro Devices X86-64'
            set cpu x86_64
        case x86_64
            set class ELF64
            set machine 'Advanced Micro Devices X86-64'
            set cpu x86_64
        case aarch64
            set class ELF64
            set machine AArch64
            set cpu aarch64
        case '*'
            return
    end
    for f in (find $dir -type f)
        set -l target (string match -r '^usr/(?:lib/gcc/)?([^/]+-[^/]+)/' -- (string replace -- $dir/ '' $f))[2]
        if test -n "$target"; and not string match -q -- "$cpu-*" $target
            continue
        end
        set -l header (readelf -h -- $f 2>/dev/null)
        or continue
        set -l got_class (string match -r '^\s*Class:\s+(\S+)' -- $header)[2]
        set -l got_machine (string match -r '^\s*Machine:\s+(.+)$' -- $header)[2]
        # An archive without members, such as glibc's libdl.a, has no ELF header to check
        test -n "$got_class"; or continue
        if test "$got_class" != $class -o "$got_machine" != $machine
            set -ga makep_problems "$name: "(string replace -- $dir/ '' $f)" is $got_class $got_machine, not $class $machine for $CARCH (use options !elfcheck if intended)"
        end
    end
end
