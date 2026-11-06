package puregogen

import "golang.org/x/sys/windows"

// String conversion

func BytePtrToString(p *byte) string {
	return windows.BytePtrToString(p)
}

func BytePtrFromString(s string) *byte {
	p, err := windows.BytePtrFromString(s)
	if err != nil {
		panic(err)
	}
	return p
}

func ByteSliceFromString(s string) []byte {
	p, err := windows.ByteSliceFromString(s)
	if err != nil {
		panic(err)
	}
	return p
}

func ByteSliceToString(s []byte) string {
	return windows.ByteSliceToString(s)
}
