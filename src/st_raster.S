| Raster palette splitter ("copper bars").
|
| Timer B counts display lines; every RASTER_STEP lines it fires and the handler writes two palette entries from
| a table: colour 0 (the background of the playing field and the HUD) and colour 5 (the frame). The same 16
| colours are therefore re-programmed down the screen, which gives smooth gradients, scrolling colour bars and
| sky/horizon fades without a single extra pixel being drawn - the classic ST demo-scene trick.
| The VBL handler restarts the timer and loads the first pair; it then chains to the TOS vertical blank.
|
| st_raster_table holds RASTER_ROWS pairs of words (colour 0, colour 5). Pair n applies from line n * RASTER_STEP.
| Must be installed and removed from supervisor mode.

	.equ	vbl_vec, 0x70
	.equ	tb_vec, 0x120
	.equ	iera, 0xfffffa07
	.equ	imra, 0xfffffa13
	.equ	isra, 0xfffffa0f
	.equ	tbcr, 0xfffffa1b
	.equ	tbdr, 0xfffffa21
	.equ	pal0, 0xffff8240
	.equ	pal5, 0xffff824a
	.equ	RASTER_STEP, 2

	.text
	.globl	_st_raster_install
	.globl	_st_raster_remove
	.globl	_st_raster_table
	.globl	_st_raster_enabled

_st_raster_install:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	vbl_vec.w,old_vbl
	move.l	tb_vec.w,old_tb
	clr.b	tbcr.w
	move.b	#RASTER_STEP,tbdr.w
	move.l	#tb_isr,tb_vec.w
	move.l	#vbl_isr,vbl_vec.w
	bclr	#0,isra.w
	bset	#0,iera.w
	bset	#0,imra.w
	move.w	(%sp)+,%sr
	rts

_st_raster_remove:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	clr.b	tbcr.w
	bclr	#0,imra.w
	bclr	#0,iera.w
	bclr	#0,isra.w
	move.l	old_tb,tb_vec.w
	move.l	old_vbl,vbl_vec.w
	move.w	(%sp)+,%sr
	rts

vbl_isr:
	tst.w	_st_raster_enabled
	beq.s	1f
	move.l	%a0,-(%sp)
	clr.b	tbcr.w
	lea	_st_raster_table,%a0
	move.w	(%a0)+,pal0.w
	move.w	(%a0)+,pal5.w
	move.l	%a0,row_ptr
	move.b	#RASTER_STEP,tbdr.w
	move.b	#8,tbcr.w		| event count mode: one count per display line
	move.l	(%sp)+,%a0
1:	move.l	old_vbl,-(%sp)
	rts

tb_isr:
	move.l	%a0,-(%sp)
	move.l	row_ptr,%a0
	move.w	(%a0)+,pal0.w
	move.w	(%a0)+,pal5.w
	move.l	%a0,row_ptr
	move.l	(%sp)+,%a0
	bclr	#0,isra.w		| software end-of-interrupt mode
	rte

	.bss
	.even
old_vbl:	.ds.l	1
old_tb:		.ds.l	1
row_ptr:	.ds.l	1
_st_raster_enabled:	.ds.w	1
_st_raster_table:	.ds.w	2*112
