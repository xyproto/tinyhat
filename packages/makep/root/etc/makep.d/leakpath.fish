# Paths of the build in the package (like /startdir/src in a binary, a .pc or a CMake file): they point at a directory
# that does not exist on Tiny Hat, and make packages differ between builds. Allowed with options leakpath
function makep_leakpath --on-event makep_check -a name dir
    contains -- leakpath $options; and return
    for f in (grep -rlaF -- $startdir/ $dir 2>/dev/null)
        set -ga makep_problems "$name: "(string replace -- $dir/ '' $f)" has the build path $startdir/ (use options leakpath if intended)"
    end
end
