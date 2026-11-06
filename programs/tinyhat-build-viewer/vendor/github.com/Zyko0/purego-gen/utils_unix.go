//go:build darwin || freebsd || linux

package puregogen

import "golang.org/x/sys/unix"

// String conversion

func BytePtrToString(p *byte) string {
	return unix.BytePtrToString(p)
}

func BytePtrFromString(s string) *byte {
	p, err := unix.BytePtrFromString(s)
	if err != nil {
		panic(err)
	}
	return p
}

func ByteSliceFromString(s string) []byte {
	p, err := unix.ByteSliceFromString(s)
	if err != nil {
		panic(err)
	}
	return p
}

func ByteSliceToString(s []byte) string {
	return unix.ByteSliceToString(s)
}
