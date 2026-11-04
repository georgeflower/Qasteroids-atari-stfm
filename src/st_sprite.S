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
