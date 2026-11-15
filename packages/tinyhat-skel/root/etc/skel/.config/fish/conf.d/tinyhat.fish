set -g fish_greeting
if status is-login; and test -z "$WAYLAND_DISPLAY"; and test "$XDG_VTNR" = 1
    tinyhat-session
end
