#!/bin/sh
if command -v tinyhat-dosbox >/dev/null; then
	exec tinyhat-dosbox -c "HATTOSS /ADLIB" -c exit
fi
exec dosbox-x -fastlaunch -c "MOUNT C /usr/share/tinyhat/dos" -c "C:" -c "HATTOSS /ADLIB" -c exit
