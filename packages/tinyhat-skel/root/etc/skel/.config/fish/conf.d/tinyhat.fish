set -g fish_greeting
if status is-login; and test -z "$WAYLAND_DISPLAY"; and test "$XDG_VTNR" = 1; and not contains tinyhat.fish (string split " " </proc/cmdline)
    tinyhat-session
end
