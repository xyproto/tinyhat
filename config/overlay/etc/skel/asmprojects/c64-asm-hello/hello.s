; Hello C64: prints a greeting, plays a short tune on the SID and cycles the border colours until a key is pressed.
; Press Ctrl+Space in this editor to build, or press it twice in rapid succession to build and run it in VICE.

CHROUT     = $FFD2
GETIN      = $FFE4
RASTER     = $D012
BORDER     = $D020
BACKGROUND = $D021
SID_FREQ   = $D400
SID_CTRL   = $D404
SID_AD     = $D405
SID_SR     = $D406
SID_VOLUME = $D418
frame      = $FB

.segment "CODE"

start:
        lda #0
        sta BORDER
        lda #6
        sta BACKGROUND
        ldx #<greeting
        ldy #>greeting
        jsr print

        lda #$0F
        sta SID_VOLUME
        lda #$09
        sta SID_AD
        lda #$00
        sta SID_SR
        ldx #0
tune:   lda notes_lo,x
        sta SID_FREQ
        lda notes_hi,x
        sta SID_FREQ+1
        lda #$11
        sta SID_CTRL
        lda rainbow,x
        sta BORDER
        ldy #12
hold:   jsr wait_frame
        dey
        bne hold
        lda #$10
        sta SID_CTRL
        inx
        cpx #8
        bne tune

        lda #0
        sta frame
spin:   jsr wait_frame
        inc frame
        lda frame
        lsr
        lsr
        lsr
        and #7
        tax
        lda rainbow,x
        sta BORDER
        jsr GETIN
        beq spin

        lda #0
        sta SID_VOLUME
        lda #14
        sta BORDER
        lda #6
        sta BACKGROUND
        ldx #<goodbye
        ldy #>goodbye
        jmp print

print:  stx $FC
        sty $FD
        ldy #0
@next:  lda ($FC),y
        beq @done
        jsr CHROUT
        iny
        bne @next
@done:  rts

wait_frame:
        lda RASTER
        cmp #250
        bne wait_frame
@same:  cmp RASTER
        beq @same
        rts

.segment "RODATA"

greeting:
        .byte $93, $0E, $05, 13, 13, 13, "            Hello from Tiny Hat!", 13, 13
        .byte $9E, "  Written in 6502 assembly with ca65.", 13, 13
        .byte $9F, "    Press any key to return to BASIC.", 0
goodbye:
        .byte $93, $8E, $9A, 0
notes_lo:
        .lobytes 4455, 5001, 5613, 5947, 6676, 7493, 8411, 8911
notes_hi:
        .hibytes 4455, 5001, 5613, 5947, 6676, 7493, 8411, 8911
rainbow:
        .byte 2, 8, 7, 13, 3, 14, 4, 1
