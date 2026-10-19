| SPDX-License-Identifier: GPL-3.0-or-later
| Input Pre-Fader Sends for the Digitakt mk1, OS 1.54.
|
| The render's mixer (0x40071c20) loops over ten channels: tracks 1-8, then
| IN L and IN R (d2 = 8, 9). For each one it builds two pan gains in d0/d1,
| multiplies them by the channel's LEV gain to get the dry gains, and then
| derives the delay and reverb sends from those dry gains, so the sends are
| post-fader. With SETTINGS > INPUT SENDS PRE ticked, the external inputs'
| sends are taken from the pan gains before LEV instead. Tracks are never
| changed, and with the row unticked both handlers run exactly the
| instructions they replace.
|
| Both sites are jsr patches that return with rts, so they also work from
| digihealth's FAST AUDIO copy of the render in SRAM.

        .equ STR_CTOR,   0x4017af20     | std::string(this, const char*, alloc&)
        .equ INVALIDATE, 0x400c9a3a     | View::invalidate(view)
        .equ BLIT,       0x400c2b88     | blit(dst, src, x, y, centre)
        .equ CHECKBOXES, 0x421f8a3c     | -> Bitmap[2], 0x1c each: empty, ticked

        .section .run, "ax"

| ---- 0x40071e36: the dry gains (8 bytes: the two msac.l below) ------------
| d0/d1 hold this channel's pan gains, d6 its LEV gain. Keep the pan gains
| for the sends, then make the dry gains as stock does. Only acc0/acc1 change.
        .balign 2
        .globl  ipf_dry
ipf_dry:
        move.l  %d0, ipf_pan_l
        move.l  %d1, ipf_pan_r
        msac.l  %d0, %d6, %acc0         | the instructions this replaced
        msac.l  %d1, %d6, %acc1
        addq.l  #2, (%sp)               | past the site's nop padding
        rts

| ---- 0x40071eb4: the sends (24 bytes) ---------------------------------------
| In: d0/d1 the dry gains, a0 the delay level, d6 the reverb level, d2 the
| channel. Out, as stock: a1/a0 the delay sends, d6/d1 the reverb sends.
| For IN L and IN R in PRE, the pan gains stand in for the dry gains; d0 is
| overwritten right after this block, so changing it here is safe.
        .balign 2
        .globl  ipf_sends
ipf_sends:
        tst.b   ipf_pre
        beq.s   1f
        cmpi.l  #8, %d2
        blt.s   1f                      | a track: stock
        move.l  ipf_pan_l, %d0
        move.l  ipf_pan_r, %d1
1:      msac.l  %d0, %a0, %acc0         | the instructions this replaced
        msac.l  %d1, %a0, %acc1
        movclr.l %acc0, %a1
        movclr.l %acc1, %a0
        msac.l  %d0, %d6, %acc0
        msac.l  %d1, %d6, %acc1
        movclr.l %acc0, %d6
        movclr.l %acc1, %d1
        addq.l  #8, (%sp)               | resume after the site's 24 bytes:
        addq.l  #8, (%sp)               | 18 bytes of nop padding, skipped
        addq.l  #2, (%sp)               | without touching a register
        rts

| ---- ev_settings: the INPUT SENDS PRE row -----------------------------------
| A checkbox row like the stock ones: YES or RIGHT ticks it, YES or LEFT
| unticks it. Unticked (POST, stock) at every power-on; not saved.
        .balign 2
        .globl  ipf_settings
ipf_settings:
        pea     ipf_row
        move.l  8(%sp), -(%sp)          | the menu
        jsr     core_additem
        addq.l  #8, %sp
        rts

ipf_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        pea     -1(%a6)
        pea     ipf_name
        move.l  %a0, %d2
        move.l  %a0, -(%sp)             | the string to build
        jsr     STR_CTOR
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts

ipf_select:
        move.l  %d2, -(%sp)
        movea.l 8(%sp), %a0
        move.l  (%a0), %d2              | the menu
        tst.b   ipf_pre
        bne.s   2f
1:      moveq   #1, %d0
        move.b  %d0, ipf_pre
        bra.s   3f
2:      clr.b   ipf_pre
3:      movea.l %d2, %a0
        lea     0x38(%a0), %a0
        move.l  (%sp)+, %d2
        move.l  %a0, 4(%sp)
        jmp     INVALIDATE              | redraw the menu

ipf_change:
        move.l  12(%sp), %d0            | delta
        beq.s   9f
        move.l  %d2, -(%sp)
        movea.l 8(%sp), %a0
        move.l  (%a0), %d2
        tst.l   %d0
        bpl.s   1b                      | RIGHT: PRE
        bra.s   2b                      | LEFT: POST
9:      rts

ipf_draw:
        moveq   #0, %d0
        tst.b   ipf_pre
        beq.s   1f
        moveq   #0x1c, %d0              | the ticked box
1:      add.l   CHECKBOXES, %d0
        move.l  12(%sp), %d1
        move.l  %d1, 4(%sp)
        move.l  %d0, 8(%sp)
        move.l  16(%sp), %d1
        move.l  %d1, 12(%sp)
        move.l  20(%sp), %d1
        move.l  %d1, 16(%sp)
        clr.l   20(%sp)
        jmp     BLIT

        .balign 4
ipf_row:    .long   ipf_label, ipf_select, ipf_draw, ipf_change
ipf_name:   .asciz  "INPUT SENDS PRE"
        .balign 2

        .section .bss
        .balign 4
ipf_pan_l:  .skip 4             | this channel's pan gains, before LEV
ipf_pan_r:  .skip 4
ipf_pre:    .skip 4             | byte 0: the row is ticked
