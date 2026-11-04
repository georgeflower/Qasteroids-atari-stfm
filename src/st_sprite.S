| Masked 16-pixel-wide sprite blitter for the ST's four-plane screen.
|
| void st_blit_masked(const uint16_t *src, unsigned char *dst, long shift, long rows)
|
| src: per row five words (planes 0-3 and the coverage mask), bit 15 the leftmost pixel; dst: the first row's
| left 16-pixel group (the right group follows 8 bytes later); shift: 0..15 pixels to the right. Each row is
| screen = (screen & ~mask) | plane in both groups.

	.text
	.globl	_st_blit_masked

_st_blit_masked:
	movem.l	%d2-%d5,-(%sp)
	move.l	20(%sp),%a0
	move.l	24(%sp),%a1
	move.l	28(%sp),%d1
	move.l	32(%sp),%d5
	subq.w	#1,%d5
1:	moveq	#0,%d0
	move.w	8(%a0),%d0			| coverage, shifted into a long: high word left group, low word right group
	swap	%d0
	lsr.l	%d1,%d0
	not.l	%d0				| keep masks
	move.w	%d0,%d4				| keep, right group
	swap	%d0				| keep, left group
	.irp	plane,0,1,2,3
	moveq	#0,%d2
	move.w	\plane*2(%a0),%d2
	swap	%d2
	lsr.l	%d1,%d2
	move.w	8+\plane*2(%a1),%d3		| right group
	and.w	%d4,%d3
	or.w	%d2,%d3
	move.w	%d3,8+\plane*2(%a1)
	swap	%d2
	move.w	\plane*2(%a1),%d3		| left group
	and.w	%d0,%d3
	or.w	%d2,%d3
	move.w	%d3,\plane*2(%a1)
	.endr
	lea	10(%a0),%a0
	lea	160(%a1),%a1
	dbra	%d5,1b
	movem.l	(%sp)+,%d2-%d5
	rts

| Execute generated rock rows with whole-group horizontal clipping. The generated stream contains
| four-word move.l/ori.l instructions; displacement is relative to the rock's centre row.
| void st_blit_rock_clipped(code, row_offsets, rows, dst_row, centre_column_bytes, row_displacement)
	.globl	_st_blit_rock_clipped
_st_blit_rock_clipped:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)
	move.l	40(%sp),%a0
	move.l	44(%sp),%a2
	move.l	48(%sp),%d7
	move.l	52(%sp),%a1
	move.l	56(%sp),%d6
	move.l	60(%sp),%d5
	subq.w	#1,%d7
rockcliprow:
	moveq	#0,%d0
	move.w	(%a2)+,%d0
	add.l	%d0,%d0
	lea	0(%a0,%d0.l),%a3
	moveq	#0,%d0
	move.w	(%a2),%d0
	add.l	%d0,%d0
	lea	0(%a0,%d0.l),%a4
	cmpa.l	%a4,%a3
	bcc.s	rockclipnextrow
rockclipword:
	move.w	6(%a3),%d0
	sub.w	%d5,%d0
	add.w	%d6,%d0
	cmp.w	#8,%d0
	blt.s	rockclipskip
	cmp.w	#152,%d0
	bge.s	rockclipskip
	move.l	2(%a3),%d1
	cmp.w	#0x217c,(%a3)
	bne.s	rockclipor
	move.l	%d1,0(%a1,%d0.w)
	bra.s	rockclipskip
rockclipor:
	or.l	%d1,0(%a1,%d0.w)
rockclipskip:
	addq.l	#8,%a3
	cmpa.l	%a4,%a3
	bcs.s	rockclipword
rockclipnextrow:
	lea	160(%a1),%a1
	add.w	#160,%d5
	dbra	%d7,rockcliprow
	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts
