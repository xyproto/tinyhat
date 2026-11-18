# Files where Tiny Hat has none: links of the filesystem package (usr/sbin is bin, lib64 is lib...), directories of
# the running system (/run, /tmp, /home...) and /usr/local, which is the administrator's. Only filesystem itself may
# have these
function makep_pathcheck --on-event makep_check -a name dir
    test "$pkgbase" = filesystem; and return
    for p in bin sbin lib lib64 usr/sbin usr/lib64 usr/local home root tmp var/tmp run var/run var/lock dev proc sys mnt
        if test -e $dir/$p; or test -L $dir/$p
            set -ga makep_problems "$name: has /$p, where Tiny Hat packages have no files"
        end
    end
end
