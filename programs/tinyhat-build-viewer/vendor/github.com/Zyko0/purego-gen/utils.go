package puregogen

func BoolToUintptr(b bool) uintptr {
	if b {
		return 1
	}
	return 0
}
