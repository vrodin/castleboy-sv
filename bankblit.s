; bankblit.s - bank-safe copy of a sprite slice out of a switchable ROM bank.
;
; cc65 emits JSRs to C-runtime helpers (in bank 0) even for trivial C loops, so
; the copy that runs while a non-zero ROM bank is mapped MUST be hand-written in
; assembly that uses only zero-page/absolute addressing and NO jsr.
;
; void bank_copy_from(unsigned char bank);
;   maps `bank` at $8000-$BFFF (preserving the $2026 base bits in _g_bankShadow),
;   copies _psb_copy_n bytes from _psb_fr -> _bank_scratch, then restores bank 0.
; All reads of the banked sprite data happen between the two $2026 writes; the
; loop touches only zero page + absolute memory, never calling into any bank.

        .export _bank_copy_from
        .export _bank_copy_batch
        .import _g_bankShadow, _psb_fr, _psb_copy_n, _bank_scratch
        .import _batch_scratch, _batch_src, _batch_len, _batch_dst, _batch_n
        .importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

        .segment "FIXEDCODE"      ; must live in the fixed bank ($C000-$FFFF)

SV_BANK = $2026

.proc _bank_copy_from
        ; A = bank number (cc65 passes the single char arg in A)
        asl  a
        asl  a
        asl  a
        asl  a
        asl  a                    ; bank << 5
        ora  _g_bankShadow        ; | base control bits (NMI/display)
        sta  SV_BANK              ; --- map the sprite bank ---

        ; source pointer (ptr1) = _psb_fr, dest (ptr2) = _bank_scratch
        lda  _psb_fr
        sta  ptr1
        lda  _psb_fr+1
        sta  ptr1+1
        lda  #<_bank_scratch
        sta  ptr2
        lda  #>_bank_scratch
        sta  ptr2+1

        ; count (tmp1=low, tmp2=high) = _psb_copy_n
        lda  _psb_copy_n
        sta  tmp1
        lda  _psb_copy_n+1
        sta  tmp2

        ldy  #0
loop:
        lda  tmp1                 ; while (count != 0)
        ora  tmp2
        beq  done

        lda  (ptr1),y
        sta  (ptr2),y

        inc  ptr1                 ; ++src
        bne  :+
        inc  ptr1+1
:       inc  ptr2                 ; ++dst
        bne  :+
        inc  ptr2+1

:       lda  tmp1                 ; --count
        bne  :+
        dec  tmp2
:       dec  tmp1
        jmp  loop

done:
        lda  _g_bankShadow        ; bank 0 = base bits only
        sta  SV_BANK              ; --- restore bank 0 ---
        rts
.endproc

; ---------------------------------------------------------------------------
; void bank_copy_batch(unsigned char bank);
;   maps `bank` ONCE, copies every queued slice
;     _batch_src[i] (in the mapped bank) -> _batch_scratch + _batch_dst[i],
;     _batch_len[i] bytes (1..255 each), for i in 0.._batch_n-1,
;   then restores bank 0. One bank round trip for the whole frame's enemies.
;   tmp3 = slot index, tmp4 = byte counter within a slice.

.proc _bank_copy_batch
        asl  a
        asl  a
        asl  a
        asl  a
        asl  a                    ; bank << 5
        ora  _g_bankShadow
        sta  SV_BANK              ; --- map the sprite bank ---

        lda  #0
        sta  tmp3                 ; slot index = 0

slot_loop:
        lda  tmp3
        cmp  _batch_n
        bcs  bdone                ; index >= count -> finished

        asl  a                    ; A = index*2 (word-array index; index<=11)
        tax
        lda  _batch_src,x         ; ptr1 = _batch_src[index]
        sta  ptr1
        lda  _batch_src+1,x
        sta  ptr1+1

        clc                       ; ptr2 = _batch_scratch + _batch_dst[index]
        lda  #<_batch_scratch
        adc  _batch_dst,x
        sta  ptr2
        lda  #>_batch_scratch
        adc  _batch_dst+1,x
        sta  ptr2+1

        ldx  tmp3                 ; byte count = _batch_len[index]
        lda  _batch_len,x
        sta  tmp4
        beq  next_slot            ; zero-length: skip

        ldy  #0
byte_loop:
        lda  (ptr1),y
        sta  (ptr2),y
        inc  ptr1
        bne  :+
        inc  ptr1+1
:       inc  ptr2
        bne  :+
        inc  ptr2+1
:       dec  tmp4
        bne  byte_loop

next_slot:
        inc  tmp3
        jmp  slot_loop

bdone:
        lda  _g_bankShadow
        sta  SV_BANK              ; --- restore bank 0 ---
        rts
.endproc
