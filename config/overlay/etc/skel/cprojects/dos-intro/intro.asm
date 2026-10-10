; A 256 byte demoscene intro: a moving rainbow pattern in 320x200 VGA mode 13h. Esc quits.
; Press Ctrl+Space twice in Orbiton to build it with nasm and run it in DOSBox-X.
; Demosceners make whole intros in 256 bytes or less. How small can you make it?
org 100h

    mov al, 13h             ; 320x200 with 256 colours
    int 10h
    push 0a000h             ; ES points to the video memory
    pop es

frame:
    mov ax, 0cccdh          ; multiplying the pixel offset by 0cccdh
    mul di                  ; gives the row in DH and the column in DL
    mov al, dl
    xor al, dh              ; a XOR pattern
    add al, bl              ; that moves over time
    shr al, 3
    add al, 32              ; colours 32 and up are a rainbow
    stosb
    test di, di             ; DI wraps around to 0 after the whole screen
    jnz frame

    inc bx                  ; next frame
    mov dx, 3dah
.wait:                      ; wait for the vertical retrace, for a smooth animation
    in al, dx
    test al, 8
    jz .wait

    in al, 60h              ; read the keyboard
    dec al                  ; Esc has scan code 1
    jnz frame

    mov ax, 3               ; back to text mode
    int 10h
    ret
