; Hello SNES: shows two lines of text on a background that slowly changes colour.
; Build with "make", run it in ZSNES with "make run".

.p816

INIDISP  = $2100
BG1SC    = $2107
VMAIN    = $2115
VMADDL   = $2116
VMDATAL  = $2118
VMDATAH  = $2119
CGADD    = $2121
CGDATA   = $2122
TM       = $212C
NMITIMEN = $4200
RDNMI    = $4210

TILEMAP  = $0400

; Writes a line of text to the tilemap, using the given palette (0-7)
.macro print row, col, text, len, palette
    rep #$20
    .a16
    lda #TILEMAP + row * 32 + col
    sta VMADDL
    sep #$20
    .a8
    ldx #0
:   lda text,x
    sta VMDATAL
    lda #palette << 2
    sta VMDATAH
    inx
    cpx #len
    bne :-
.endmacro

.segment "ZEROPAGE"
frame: .res 2
color: .res 2

.segment "CODE"

reset:
    sei
    clc
    xce                     ; leave 6502 emulation mode
    rep #$38
    .a16
    .i16
    ldx #$1FFF
    txs
    lda #0
    tcd
    sta frame
    sep #$20
    .a8

    lda #$8F                ; screen off while setting up
    sta INIDISP
    ldx #$33
:   stz INIDISP,x
    dex
    bne :-
    stz NMITIMEN

    lda #$80                ; step the VRAM address after each high byte
    sta VMAIN

    ldx #0                  ; font tiles, one bitplane, at VRAM $0000
    stx VMADDL
:   lda font,x
    sta VMDATAL
    stz VMDATAH
    inx
    cpx #font_size
    bne :-

    ldx #TILEMAP            ; clear the 32x32 tilemap
    stx VMADDL
    ldx #32 * 32
:   stz VMDATAL
    stz VMDATAH
    dex
    bne :-

    print 12, 10, hello, hello_len, 0
    print 14, 12, tinyhat, tinyhat_len, 1

    stz CGADD
    ldx #0
:   lda palette,x
    sta CGDATA
    inx
    cpx #palette_size
    bne :-

    lda #TILEMAP >> 8
    sta BG1SC
    lda #$01                ; show background 1
    sta TM
    lda #$0F                ; screen on, full brightness
    sta INIDISP
    lda #$80                ; NMI at every vertical blank
    sta NMITIMEN

forever:
    wai
    bra forever

; Runs at every vertical blank, 60 times per second
nmi:
    rep #$30
    .a16
    .i16
    pha
    phx
    sep #$20
    .a8
    lda RDNMI
    rep #$20
    .a16
    inc frame
    lda frame               ; blue goes up and down between 4 and 19
    lsr
    lsr
    lsr
    and #$1F
    cmp #$10
    bcc :+
    eor #$1F
:   clc
    adc #4
    xba
    asl
    asl
    ora #(3 << 5) | 2
    sta color
    sep #$20
    .a8
    stz CGADD
    lda color
    sta CGDATA
    lda color + 1
    sta CGDATA
    rep #$30
    plx
    pla
    rti

empty:
    rti

.segment "RODATA"

; Colours are 0bbbbbgg gggrrrrr, low byte first
palette:
    .word (8 << 10) | (3 << 5) | 2, $7FFF, 0, 0
    .word 0, (18 << 10) | (19 << 5) | 3, 0, 0
palette_size = * - palette

; The text uses the tile numbers of the font below
.pushcharmap
.charmap ' ', 0
.charmap 'H', 1
.charmap 'E', 2
.charmap 'L', 3
.charmap 'O', 4
.charmap 'S', 5
.charmap 'N', 6
.charmap '!', 7
.charmap 'T', 8
.charmap 'I', 9
.charmap 'Y', 10
.charmap 'A', 11
hello:   .byte "HELLO SNES!"
hello_len = * - hello
tinyhat: .byte "TINY HAT"
tinyhat_len = * - tinyhat
.popcharmap

font:
    .byte $00, $00, $00, $00, $00, $00, $00, $00 ; space
    .byte $66, $66, $66, $7E, $66, $66, $66, $00 ; H
    .byte $7E, $60, $60, $7C, $60, $60, $7E, $00 ; E
    .byte $60, $60, $60, $60, $60, $60, $7E, $00 ; L
    .byte $3C, $66, $66, $66, $66, $66, $3C, $00 ; O
    .byte $3C, $66, $60, $3C, $06, $66, $3C, $00 ; S
    .byte $66, $76, $7E, $7E, $6E, $66, $66, $00 ; N
    .byte $18, $18, $18, $18, $18, $00, $18, $00 ; !
    .byte $7E, $18, $18, $18, $18, $18, $18, $00 ; T
    .byte $3C, $18, $18, $18, $18, $18, $3C, $00 ; I
    .byte $66, $66, $66, $3C, $18, $18, $18, $00 ; Y
    .byte $3C, $66, $66, $7E, $66, $66, $66, $00 ; A
font_size = * - font

.segment "HEADER"
    .byte "HELLO SNES           "   ; title, 21 characters
    .byte $20                       ; LoROM
    .byte $00                       ; ROM only
    .byte $05                       ; 32 KiB
    .byte $00                       ; no cartridge RAM
    .byte $01                       ; North America
    .byte $00                       ; developer
    .byte $00                       ; version
    .word $FFFF                     ; checksum complement, set by checksum.sh
    .word $0000                     ; checksum, set by checksum.sh

.segment "VECTORS"
    .word 0, 0, empty, empty, empty, nmi, 0, empty      ; native mode
    .word 0, 0, empty, 0, empty, empty, reset, empty    ; emulation mode
