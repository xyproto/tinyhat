set -g fish_greeting

set -q BROWSER; or set -gx BROWSER /usr/bin/netsurfy
set -q EDITOR; or set -gx EDITOR /usr/bin/o
set -q MANPAGER; or set -gx MANPAGER /usr/bin/o
set -q O_THEME; or set -gx O_THEME xoria

# OpenWatcom
set -gx WATCOM /usr/lib/watcom
set -gx INCLUDE $WATCOM/h
contains -- $WATCOM/binl $PATH; or set -a PATH $WATCOM/binl

# DosBox and emulators
set -gx DOSBOX_CONF /etc/tinyhat/dosbox-x.conf:/run/tinyhat-midi/dosbox-x.conf
set -gx PVSNESLIB_HOME /usr/share/pvsneslib
contains -- $PVSNESLIB_HOME/devkitsnes/bin $PATH; or set -a PATH $PVSNESLIB_HOME/devkitsnes/bin

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
    if set -q argv[1]
        builtin history $argv
    else
        builtin history --show-time='%F %T '
    end
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

alias ls='command ls --color=auto --group-directories-first'
alias la='command ls -A --color=auto --group-directories-first'
alias ll='command ls -l --color=auto --group-directories-first'
alias l.="command ls -A | grep -e '^\.'"
alias tarnow='tar acf '
alias untar='tar xvf '
alias psmem='ps auxf | sort -nr -k 4'
alias psmem10='ps auxf | sort -nr -k 4 | head -10'
alias ..='cd ..'
alias ...='cd ../..'
alias ....='cd ../../..'
alias .....='cd ../../../..'
alias grep='grep --color=auto'
alias mv='mv -i'

alias neofetch='fastfetch'

alias update='doas /usr/lib/tinyhat/pacman -Syu'

function cleanup --description 'Remove packages that nothing needs any more'
    set -l orphans (pacman -Qtdq)
    if set -q orphans[1]
        doas /usr/lib/tinyhat/pacman -Rns $orphans
    else
        echo 'No orphaned packages.'
    end
end

function wget --description 'Download files with curl (wget -O, -q, -c and several URLs)'
    set -l opts -fL --retry 3
    set -l out
    set -l urls
    set -l i 1
    while test $i -le (count $argv)
        set -l a $argv[$i]
        switch $a
            case -O --output-document
                set i (math $i + 1)
                set out $argv[$i]
            case '-O?*'
                set out (string sub -s 3 -- $a)
            case '--output-document=*'
                set out (string replace -- --output-document= '' $a)
            case -q --quiet
                set -a opts -s
            case -nv --no-verbose
                set -a opts -sS
            case -c --continue
                set -a opts -C -
            case '-*'
                echo "wget: $a is not supported here, use curl directly" >&2
                return 2
            case '*'
                set -a urls $a
        end
        set i (math $i + 1)
    end
    if not set -q urls[1]
        echo 'usage: wget [-q] [-c] [-O FILE] URL...' >&2
        return 2
    end
    if test -z "$out"
        command curl $opts --remote-name-all -- $urls
    else if test "$out" = -
        command curl $opts -- $urls
    else
        command curl $opts -- $urls >$out
    end
end

function ssh --description 'ssh, with a TERM that machines without foot\'s terminfo know'
    if test "$TERM" = foot
        TERM=xterm-256color command ssh $argv
    else
        command ssh $argv
    end
end
alias fixpacman='doas rm /var/lib/pacman/db.lck'

alias jctl='journalctl -p 3 -xb'

alias s='git status'
alias d='git diff'
alias gd='git diff --cached'

alias week='date +%V'
alias ukenr='date +%V'

function __dos_args
    string match -rv '^/[A-Za-z?]$' -- $argv
end

function dir --description 'List files (DOS: DIR)'
    command ls -l --color=auto --group-directories-first (__dos_args $argv)
end

function del --description 'Delete files (DOS: DEL)'
    command rm -I (__dos_args $argv)
end

function deltree --description 'Delete a directory and everything in it (DOS: DELTREE)'
    command rm -rI (__dos_args $argv)
end

function tree --description 'Show the directory tree (DOS: TREE)'
    set -l dirs (__dos_args $argv)
    set -q dirs[1]; or set dirs .
    for d in $dirs
        find $d -not -path '*/.*' | sort | sed -e 's|[^/]*/|  |g'
    end
end

function ver --description 'Show the system version (DOS: VER)'
    echo "Tiny Hat on Linux $(uname -r) ($(uname -m))"
end

alias cls='clear'
alias edit='o'
alias sl='/usr/bin/sl -a -d -e -w -3'
alias md='mkdir -p'
alias mtr='doas mtr'
alias rd='rmdir'
alias ren='mv -i'
alias move='mv -i'
alias xcopy='cp -r'
alias mem='free -h'
alias cd..='cd ..'

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
        'tree' 'find' 'show the directory tree' \
        'mem' 'free -h' 'show memory use' \
        'ver' 'uname -a' 'show the system version'
    echo
    echo 'Paths use / instead of \\ and names are case sensitive. USB sticks and CDs are in /media.'
    echo 'Tab completes names, Up repeats commands, and "man ls" explains any command.'
end

if type -q o; and type -q fastfetch
  if mkdir /tmp/.tinyhat-fastfetch 2>/dev/null
    fastfetch
  else
    o --upsie
  end
end
