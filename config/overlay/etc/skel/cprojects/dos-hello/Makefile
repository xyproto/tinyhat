export WATCOM = /usr/lib/watcom
export INCLUDE = $(WATCOM)/h
export PATH := $(WATCOM)/binl:$(PATH)

hello.exe: hello.c
	wcl -q -bt=dos -ms -za99 -fe=$@ hello.c

run: hello.exe
	dosbox-x -conf /etc/tinyhat/dosbox-x.conf -c "mount c ." -c "c:" -c "hello.exe" -c "exit"

clean:
	rm -f hello.exe hello.o

.PHONY: run clean
