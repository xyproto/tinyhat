set -g fish_greeting
set -gx MANPAGER o
if test (id -un) = tinyhat
    set -gx O_THEME xoria
    set -gx BROWSER netsurf-gtk3
end
set -gx WATCOM /usr/lib/watcom
set -gx INCLUDE $WATCOM/h
contains -- $WATCOM/binl $PATH; or set -a PATH $WATCOM/binl

if test -d ~/.local/bin; and not contains -- ~/.local/bin $PATH
    set -p PATH ~/.local/bin
end

status is-interactive; or return

function __history_previous_command
    switch (commandline -t)
        case "!"
            commandline -t $history[1]
            commandline -f repaint
        case "*"
            commandline -i !
    end
end

function __history_previous_command_arguments
    switch (commandline -t)
        case "!"
            commandline -t ""
            commandline -f history-token-search-backward
        case "*"
            commandline -i '$'
    end
end

bind ! __history_previous_command
bind '$' __history_previous_command_arguments

function history
    builtin history --show-time='%F %T '
end

function backup --argument filename
    cp $filename $filename.bak
end

function copy
    if test (count $argv) = 2; and test -d "$argv[1]"
        command cp -r (string trim -r -c / -- $argv[1]) $argv[2]
    else
        command cp $argv
    end
end

alias ls='lsd -al --color=always --group-directories-first --icon never'
alias la='lsd -a --color=always --group-directories-first --icon never'
alias ll='lsd -l --color=always --group-directories-first --icon never'
alias lt='lsd -a --tree --color=always --group-directories-first --icon never'
alias l.="lsd -a | grep -e '^\.'"
alias tarnow='tar -acf '
alias untar='tar -zxvf '
alias psmem='ps auxf | sort -nr -k 4'
alias psmem10='ps auxf | sort -nr -k 4 | head -10'
alias ..='cd ..'
alias ...='cd ../..'
alias ....='cd ../../..'
alias .....='cd ../../../..'
alias grep='grep --color=auto'
alias update='doas pacman -Syu'
alias cleanup='doas pacman -Rns (pacman -Qtdq)'
alias jctl='journalctl -p 3 -xb'
alias fixpacman='doas rm /var/lib/pacman/db.lck'

function __dos_args
    string match -rv '^/[A-Za-z?]$' -- $argv
end

function dir --description 'List files (DOS: DIR)'
    lsd -l --color=always --group-directories-first --icon never (__dos_args $argv)
end

function type --description 'Show a text file (DOS: TYPE)'
    if test (count $argv) -gt 0; and not string match -q -- '-*' $argv; and test -f "$argv[1]"
        command cat $argv
    else
        builtin type $argv
    end
end

function del --description 'Delete files (DOS: DEL)'
    command rm -I (__dos_args $argv)
end

function deltree --description 'Delete a directory and everything in it (DOS: DELTREE)'
    command rm -rI (__dos_args $argv)
end

function tree --description 'Show the directory tree (DOS: TREE)'
    lsd --tree --color=always --group-directories-first --icon never (__dos_args $argv)
end

function ver --description 'Show the system version (DOS: VER)'
    echo "Tiny Hat on Linux $(uname -r) ($(uname -m))"
end

alias cls='clear'
alias edit='o'
alias sl='/usr/bin/sl -a -d -e -w -3'
alias md='mkdir -p'
alias mtr='doas mtr'
alias jomon='doas jomon'
alias rd='rmdir'
alias ren='mv -i'
alias move='mv -i'
alias xcopy='cp -r'
alias mem='free -h'
alias cd..='cd ..'

function about --description 'Show information about this Tiny Hat system'
    set -l c (set_color 7dcfff) (set_color normal)
    set -l mem (free -m | awk '/^Mem:/ { print $2 - $7 " MiB used of " $2 " MiB" }')
    set -l image unknown
    set -l backing (cat /sys/block/loop0/loop/backing_file 2>/dev/null)
    if test -n "$backing"
        set image (math --scale 0 (stat -c %s $backing) / 1048576)" MiB (squashfs, read-only)"
    end
    set -l storage "none, running live"
    if mountpoint -q /persist
        set storage (df -h --output=avail /persist | tail -n 1 | string trim)" free for your files"
    end
    set -l wlroots (path basename /usr/lib/libwlroots-*.so | string replace -r '^libwlroots-(.*)\.so$' '$1')
    set -l sdl (string replace -r -- '-[^-]*$' '' (pacman -Q sdl3 2>/dev/null | string split ' ')[2])
    set -l pipewire (pipewire --version 2>/dev/null | string match -r '[0-9][0-9.]+$' | head -n 1)
    set -l mesa (string replace -ra -- '^[0-9]+:|-[^-]*$' '' (pacman -Q mesa 2>/dev/null | string split ' ')[2])

    printf '%sTiny Hat%s\n\n' $c
    for line in \
        "Kernel|"(uname -r) \
        "CPU|"(string replace -r '.*: ' '' (grep -m1 'model name' /proc/cpuinfo)) \
        "Memory|$mem" \
        "Uptime|"(uptime -p | string replace 'up ' '') \
        "Boot time|"(systemd-analyze 2>/dev/null | head -n 1 | string replace -r '.* = ' '') \
        "System|$image" \
        "Storage|$storage" \
        "Packages|"(pacman -Q | count) \
        "Graphics|Wayland, wlroots $wlroots[-1], dwl, Mesa $mesa" \
        "Audio|PipeWire $pipewire" \
        "SDL|$sdl"
        set -l kv (string split -m 1 '|' $line)
        printf '%s%-10s%s %s\n' $c[1] $kv[1] $c[2] $kv[2]
    end
    echo
    printf '%sMost memory used by%s\n' $c
    ps -eo rss=,comm= --sort=-rss | head -n 6 | while read -l rss comm
        printf '  %-16s %5d MiB\n' $comm (math --scale 0 $rss / 1024)
    end
end

function help --description 'DOS commands and their Linux names'
    set -l c (set_color 7dcfff) (set_color normal)
    printf '%sDOS%s          %sLinux%s        What it does
' $c[1] $c[2] $c[1] $c[2]
    printf '%-12s %-12s %s
' \
        'dir' 'ls' 'list files' \
        'cd' 'cd' 'change directory (cd .. goes up, cd alone goes home)' \
        'type file' 'cat file' 'show a text file' \
        'copy a b' 'cp a b' 'copy files' \
        'xcopy a b' 'cp -r a b' 'copy a directory' \
        'move a b' 'mv a b' 'move or rename' \
        'ren a b' 'mv a b' 'rename' \
        'del file' 'rm file' 'delete files' \
        'deltree dir' 'rm -r dir' 'delete a directory and its contents' \
        'md dir' 'mkdir dir' 'make a directory' \
        'rd dir' 'rmdir dir' 'remove an empty directory' \
        'cls' 'clear' 'clear the screen' \
        'edit file' 'o file' 'edit a text file (Ctrl+S saves, Ctrl+Q quits)' \
        'tree' 'lsd --tree' 'show the directory tree' \
        'mem' 'free -h' 'show memory use' \
        'ver' 'uname -a' 'show the system version'
    echo
    echo 'Paths use / instead of \\ and names are case sensitive. USB sticks and CDs are in /media.'
    echo 'Tab completes names, Up repeats commands, and "man ls" explains any command.'
end

if test "$TERM" != linux; and type -q fastfetch
    fastfetch
end
