package puregogen

import (
	"syscall"
)

func OpenLibrary(name string) (uintptr, error) {
	handle, err := syscall.LoadLibrary(name)
	return uintptr(handle), err
}

func OpenSymbol(lib uintptr, name string) (uintptr, error) {
	return syscall.GetProcAddress(syscall.Handle(lib), name)
}

func CloseLibrary(lib uintptr) error {
	return syscall.FreeLibrary(syscall.Handle(lib))
}
