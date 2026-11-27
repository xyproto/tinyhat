; MAIN.ASM - a plasma with palette cycling, for DOS and a VGA card
;
; Edit it:      EDIT MAIN.ASM
; Compile it:   NASM -f bin -o MAIN.COM MAIN.ASM
; Run it:       MAIN
;
; Press any key to go back to DOS.
;
; How it works: the screen is switched to mode 13h (320x200 pixels, 256 colours).
; Every pixel gets a colour number from four sine waves added together. The picture
; is drawn only once. The movement comes from rotating the 256-colour palette one
; step per frame, so the colours flow through the pattern.
;
; Things to try: change the multipliers in the pixel loop, or the 85 and 170 in
; the palette loop, then compile and run it again.

        org 100h

start:
        ; Build a palette of 512 entries (red, green, blue: 0-63 each), so that
        ; any window of 256 entries can be sent to the VGA card in one go
        mov bx, sine
        mov di, palette
        xor dx, dx
        mov cx, 512
.palette:
        mov al, dl
        xlatb                   ; al = sine[al]
        shr al, 2
        stosb                   ; red
        mov al, dl
        add al, 85
        xlatb
        shr al, 2
        stosb                   ; green
        mov al, dl
        add al, 170
        xlatb
        shr al, 2
        stosb                   ; blue
        inc dl
        loop .palette

        mov ax, 13h             ; 320x200, 256 colours
        int 10h
        mov ax, 0A000h          ; video memory
        mov es, ax

        ; Draw the plasma: colour = sin(2x) + sin(3y) + sin(x+y) + sin(2(x-y))
        xor di, di
        xor dx, dx              ; dx = y
.row:
        xor cx, cx              ; cx = x
.pixel:
        mov al, cl
        add al, al
        xlatb
        mov ah, al
        mov al, dl
        add al, dl
        add al, dl
        xlatb
        add ah, al
        mov al, cl
        add al, dl
        xlatb
        add ah, al
        mov al, cl
        sub al, dl
        add al, al
        xlatb
        add al, ah
        stosb
        inc cx
        cmp cx, 320
        jb .pixel
        inc dx
        cmp dx, 200
        jb .row

        ; Rotate the palette, once per screen refresh, until a key is pressed
        xor bp, bp              ; bp = first palette entry to show
.frame:
        mov dx, 3DAh            ; wait for the start of the vertical retrace
.during:
        in al, dx
        test al, 8
        jnz .during
.before:
        in al, dx
        test al, 8
        jz .before
        mov dx, 3C8h            ; start writing at colour 0
        xor al, al
        out dx, al
        inc dx                  ; 3C9h: the palette data port
        mov si, bp
        add si, si
        add si, bp
        add si, palette         ; si = palette + bp * 3
        mov cx, 768
.colour:
        lodsb
        out dx, al
        loop .colour
        inc bp
        and bp, 255
        mov ah, 1               ; a key waiting?
        int 16h
        jz .frame
        xor ah, ah              ; take the key
        int 16h

        mov ax, 3               ; back to text mode
        int 10h
        ret

; 256 steps of a sine wave, from 0 to 255
sine:
        db 128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173
        db 176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215
        db 218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244
        db 245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255
        db 255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246
        db 245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220
        db 218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179
        db 176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131
        db 128, 124, 121, 118, 115, 112, 109, 106, 103, 100,  97,  93,  90,  88,  85,  82
        db  79,  76,  73,  70,  67,  65,  62,  59,  57,  54,  52,  49,  47,  44,  42,  40
        db  37,  35,  33,  31,  29,  27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11
        db  10,   9,   7,   6,   5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0
        db   0,   0,   0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9
        db  10,  11,  12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35
        db  37,  40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76
        db  79,  82,  85,  88,  90,  93,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124

section .bss
palette:
        resb 512 * 3
