; crt0.s - custom C startup for the Watara Supervision 64k banked build.
;
; This replaces cc65's stock supervision crt0 for one reason: the stock reset
; glue selects ROM bank 6 (STA $2026 #$C9) before jumping to the C startup. In a
; 64k cart the startup/code lives in bank 0, so the stock value would map the
; wrong bank and hang. Here the reset glue selects BANK 0 (display+NMI = $09),
; so no post-link patch (the former fix_bank0.py) is needed.
;
; Otherwise this mirrors the stock cc65 supervision crt0: zero BSS, copy DATA,
; run constructors, call main, then loop on exit.

        .export         _exit
        .export         __STARTUP__ : absolute = 1      ; mark as startup
        .import         zerobss, copydata, initlib, donelib
        .import         _main
        .import         __RAM_START__, __RAM_SIZE__

        .include        "zeropage.inc"

; --- Supervision register / bit definitions (from supervision.inc) ---
sv_bank             = $2026
SV_NMI_ENABLE_ON    = $01
SV_LCD_ON           = $08
; bank 0 mapped at $8000-$BFFF, display on, NMI OFF. The game never uses
; interrupts (it counts frames in its own loop), so NMI is left disabled and no
; interrupt handler is required.
BANK0_CTRL          = SV_LCD_ON                         ; = $08

; ------------------------------------------------------------------------
        .segment        "CODE"

start:
        sei                             ; mask IRQ (NMI is disabled via $2026).
                                        ; interrupt vectors live in bank 0, so a
                                        ; stray IRQ while a sprite bank is mapped
                                        ; would jump into the wrong bank and hang.
        cld

        ; set up the C stack pointer to the top of the available RAM
        lda     #<(__RAM_START__ + __RAM_SIZE__)
        sta     sp
        lda     #>(__RAM_START__ + __RAM_SIZE__)
        sta     sp+1

        jsr     zerobss                 ; clear BSS
        jsr     copydata                ; init DATA from its load image
        jsr     initlib                 ; run constructors

        jsr     _main                   ; -> the game

; fall through to exit if main ever returns
_exit:
        jsr     donelib                 ; run destructors
@loop:  jmp     @loop                   ; nothing to return to; spin

; Interrupts are disabled (NMI off, IRQ masked), but provide a harmless RTI so a
; stray interrupt is ignored rather than running wild.
irq_nmi:
        rti

; ------------------------------------------------------------------------
; Reset / startup glue at the top of the fixed bank ($FFF0). Selects BANK 0 (not
; cc65's default bank 6) then jumps to the C startup above.

        .segment        "FFF0"
reset:
        lda     #BANK0_CTRL             ; bank 0 + display on (was #$C9 = bank 6)
        sta     sv_bank
        jmp     start

; ------------------------------------------------------------------------
; Hardware vectors (NMI, RESET, IRQ/BRK).
        .segment        "VECTOR"
        .word           irq_nmi                 ; NMI
        .word           reset                   ; RESET
        .word           irq_nmi                 ; IRQ/BRK
