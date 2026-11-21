#!/bin/sh
if command -v tinyhat-dosbox >/dev/null; then
	exec tinyhat-dosbox -c HATTOSS -c exit
fi
exec dosbox-x -fastlaunch /usr/share/tinyhat/dos/HATTOSS.EXE -exit
