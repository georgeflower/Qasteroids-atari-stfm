#include "asm_offsets.inc"
	.text
	.globl	_st_clear_buffer
_st_clear_buffer:
	movem.l	%d2-%d6/%a2,-(%sp)
	move.l	28(%sp),%a0
	lea	32000(%a0),%a0
	moveq	#0,%d0
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	move.l	%d0,%a1
	move.l	%d0,%a2
	move.w	#999,%d6		| 1000 x 32 bytes
1:
	movem.l	%d0-%d5/%a1-%a2,-(%a0)
	dbra	%d6,1b
	movem.l	(%sp)+,%d2-%d6/%a2
	rts

| Clear a rectangle of 16-pixel groups in a low-resolution screen.
| void st_clear_rect(unsigned char *buf, long group0, long group1, long y0, long y1)   (inclusive)
| Rows of 1-4 groups use movem.l stores (d0-d5 are zero); wider rects use a loop.
	.globl	_st_clear_rect
_st_clear_rect:
	movem.l	%d2-%d7,-(%sp)
	move.l	28(%sp),%a0
	move.l	32(%sp),%d0
	move.l	36(%sp),%d1
	move.l	40(%sp),%d2
	move.l	44(%sp),%d7
	sub.l	%d0,%d1			| groups - 1
	sub.l	%d2,%d7			| rows - 1
	mulu.w	#160,%d2
	add.l	%d2,%a0
	lsl.l	#3,%d0
	add.l	%d0,%a0
	move.l	%d1,%d6			| d6 = groups - 1
	moveq	#0,%d0
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	cmp.w	#3,%d6
	bhi.s	wide
	add.w	%d6,%d6
	move.w	rowtable(%pc,%d6.w),%d6
	jmp	rowtable(%pc,%d6.w)
rowtable:
	.word	row1-rowtable, row2-rowtable, row3-rowtable, row4-rowtable
row1:	movem.l	%d0-%d1,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row1
	bra.s	clearend
row2:	movem.l	%d0-%d3,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row2
	bra.s	clearend
row3:	movem.l	%d0-%d5,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row3
	bra.s	clearend
row4:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d1,24(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row4
	bra.s	clearend
wide:	move.l	#160,%a1		| more than four groups: generic loop
	move.l	%d6,%d3
	addq.l	#1,%d3
	lsl.l	#3,%d3
	sub.l	%d3,%a1
1:	move.w	%d6,%d2
2:	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	dbra	%d2,2b
	add.l	%a1,%a0
	dbra	%d7,1b
clearend:
	movem.l	(%sp)+,%d2-%d7
	rts

| Clear a list of rectangles (x0, y0, x1, y1 as words, x in pixels, inclusive) rounded out to 16-pixel groups:
| the same as calling st_clear_rect for each, without the calls.
| void st_clear_rects(unsigned char *buf, const short *rects, long count)
	.globl	_st_clear_rects
_st_clear_rects:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)		| 36 bytes; arguments from 40(%sp)
	move.l	40(%sp),%a2
	move.l	44(%sp),%a3
	move.l	48(%sp),%d0
	ble.w	crdone
	lsl.l	#3,%d0
	lea	0(%a3,%d0.l),%a4		| a4 = end of the list
crloop:
	movem.w	(%a3)+,%d0-%d3			| x0, y0, x1, y1
	sub.w	%d1,%d3				| rows - 1
	mulu.w	#160,%d1
	lea	0(%a2,%d1.l),%a0
	lsr.w	#4,%d0
	lsr.w	#4,%d2
	sub.w	%d0,%d2				| groups - 1
	lsl.w	#3,%d0
	adda.w	%d0,%a0
	move.w	%d3,%d7
	moveq	#0,%d6
	move.w	%d2,%d6
	moveq	#0,%d0
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	cmp.w	#6,%d6
	bhi.w	crwide
	add.w	%d6,%d6
	move.w	crtable(%pc,%d6.w),%d6
	jmp	crtable(%pc,%d6.w)
crtable:
	.word	cr1-crtable, cr2-crtable, cr3-crtable, cr4-crtable, cr5-crtable, cr6-crtable, cr7-crtable
cr1:	movem.l	%d0-%d1,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr1
	bra.w	crnext
cr2:	movem.l	%d0-%d3,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr2
	bra.w	crnext
cr3:	movem.l	%d0-%d5,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr3
	bra.w	crnext
cr4:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d1,24(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr4
	bra.w	crnext
cr5:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d3,24(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr5
	bra.w	crnext
cr6:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d5,24(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr6
	bra.w	crnext
cr7:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d5,24(%a0)
	movem.l	%d0-%d3,48(%a0)
	lea	160(%a0),%a0
	dbra	%d7,cr7
	bra.w	crnext
crwide:	move.l	#160,%a1			| more than four groups: generic loop
	move.l	%d6,%d3
	addq.l	#1,%d3
	lsl.l	#3,%d3
	sub.l	%d3,%a1
1:	move.w	%d6,%d2
2:	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	dbra	%d2,2b
	add.l	%a1,%a0
	dbra	%d7,1b
crnext:
	cmp.l	%a4,%a3
	bcs.w	crloop
crdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts

| Closed polygon outline, or open polyline, in one bitplane of a low-resolution screen.
| void st_draw_poly_plane(unsigned char *buf, const short *points, long count, long plane_offset)
| void st_draw_polyline(unsigned char *buf, const short *points, long count, long plane_offset)
| points are x,y word pairs, all inside 0..319 x 0..199. plane_offset is 0, 2, 4 or 6.
| The same Bresenham as a single line, edge by edge, but the first point is plotted once and every
| edge then only steps and plots, so each edge carries on from where the last one ended: no
| per-edge address set-up and no pixel plotted twice at the corners. The loops below step first
| and plot second. The polygon has count edges (the last one back to the first point), the
| polyline count - 1.
	.globl	_st_draw_poly_plane
_st_draw_poly_plane:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	56(%sp),%d5
	beq.w	polydone
	subq.w	#1,%d5			| d5 = edges - 1, for dbra
	bra.s	polyentry
	.globl	_st_draw_polyline
_st_draw_polyline:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	56(%sp),%d5
	beq.w	polydone
	subq.w	#2,%d5
polyentry:
	move.l	48(%sp),%a2
	add.l	60(%sp),%a2		| a2 = buffer + plane offset
	move.l	52(%sp),%a3		| a3 = current point
	move.l	%a3,%a5			| a5 = first point (to close the outline)
	move.l	56(%sp),%d0
	lsl.l	#2,%d0
	lea	0(%a3,%d0.l),%a4	| a4 = end of the point list
	move.w	(%a3),%d3		| first pixel
	move.w	2(%a3),%d6
	mulu.w	#160,%d6
	lea	0(%a2,%d6.l),%a0
	move.w	%d3,%d6
	lsr.w	#4,%d6
	lsl.w	#3,%d6
	adda.w	%d6,%a0
	moveq	#15,%d6
	and.w	%d3,%d6
	move.w	#0x8000,%d0
	lsr.w	%d6,%d0			| pixel mask
	or.w	%d0,(%a0)
	tst.w	%d5
	bmi.w	polydone
edge:
	lea	4(%a3),%a6		| next point, wrapping to the first
	cmp.l	%a4,%a6
	bcs.s	1f
	move.l	%a5,%a6
1:	move.w	(%a6),%d1
	sub.w	(%a3),%d1		| dx
	move.w	2(%a6),%d2
	sub.w	2(%a3),%d2		| dy
	movea.w	#160,%a1
	tst.w	%d2
	bpl.s	2f
	neg.w	%d2
	movea.w	#-160,%a1
2:	moveq	#0,%d4
	tst.w	%d1
	bpl.s	3f
	neg.w	%d1
	moveq	#1,%d4
3:	cmp.w	%d2,%d1
	bcs.w	ymajor
	move.w	%d1,%d7			| steps along x
	beq.w	edgedone		| no steps (a zero-length edge)
	move.w	%d1,%d3
	lsr.w	#1,%d3
	subq.w	#1,%d7
	tst.w	%d4
	bne.s	xleft
xright:
	sub.w	%d2,%d3
	bpl.s	4f
	add.w	%d1,%d3
	adda.w	%a1,%a0
4:	ror.w	#1,%d0
	bcc.s	5f
	addq.l	#8,%a0
5:	or.w	%d0,(%a0)
	dbra	%d7,xright
	bra.s	edgedone
xleft:
	sub.w	%d2,%d3
	bpl.s	6f
	add.w	%d1,%d3
	adda.w	%a1,%a0
6:	rol.w	#1,%d0
	bcc.s	7f
	subq.l	#8,%a0
7:	or.w	%d0,(%a0)
	dbra	%d7,xleft
	bra.s	edgedone
ymajor:
	move.w	%d2,%d7			| steps along y
	move.w	%d2,%d3
	lsr.w	#1,%d3
	subq.w	#1,%d7
	tst.w	%d4
	bne.s	yleft
yright:
	sub.w	%d1,%d3
	bpl.s	8f
	add.w	%d2,%d3
	ror.w	#1,%d0
	bcc.s	8f
	addq.l	#8,%a0
8:	adda.w	%a1,%a0
	or.w	%d0,(%a0)
	dbra	%d7,yright
	bra.s	edgedone
yleft:
	sub.w	%d1,%d3
	bpl.s	9f
	add.w	%d2,%d3
	rol.w	#1,%d0
	bcc.s	9f
	subq.l	#8,%a0
9:	adda.w	%a1,%a0
	or.w	%d0,(%a0)
	dbra	%d7,yleft
edgedone:
	addq.l	#4,%a3
	dbra	%d5,edge
polydone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| The same polygon / polyline drawer for colours that use several bitplanes: one pass, with an ori per
| set plane at a fixed displacement (planes = the colour number, bit p = plane p at 2 * p bytes). Entered like
| st_draw_poly_plane / st_draw_polyline with plane_offset 0; there is one copy per colour 3, 5, 6, 7, 9 .. 15.
	.macro	PLOTPX planes
	.if \planes & 1
	or.w	%d0,(%a0)
	.endif
	.if \planes & 2
	or.w	%d0,2(%a0)
	.endif
	.if \planes & 4
	or.w	%d0,4(%a0)
	.endif
	.if \planes & 8
	or.w	%d0,6(%a0)
	.endif
	.endm

	.macro	POLYMULTI sfx, planes
	.globl	_st_draw_poly_m\sfx
_st_draw_poly_m\sfx:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	56(%sp),%d5
	beq.w	polydone\sfx
	subq.w	#1,%d5			| d5 = edges - 1, for dbra
	bra.s	polyentry\sfx
	.globl	_st_draw_polyline_m\sfx
_st_draw_polyline_m\sfx:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	56(%sp),%d5
	beq.w	polydone\sfx
	subq.w	#2,%d5
polyentry\sfx:
	move.l	48(%sp),%a2
	add.l	60(%sp),%a2		| a2 = buffer + plane offset
	move.l	52(%sp),%a3		| a3 = current point
	move.l	%a3,%a5			| a5 = first point (to close the outline)
	move.l	56(%sp),%d0
	lsl.l	#2,%d0
	lea	0(%a3,%d0.l),%a4	| a4 = end of the point list
	move.w	(%a3),%d3		| first pixel
	move.w	2(%a3),%d6
	mulu.w	#160,%d6
	lea	0(%a2,%d6.l),%a0
	move.w	%d3,%d6
	lsr.w	#4,%d6
	lsl.w	#3,%d6
	adda.w	%d6,%a0
	moveq	#15,%d6
	and.w	%d3,%d6
	move.w	#0x8000,%d0
	lsr.w	%d6,%d0			| pixel mask
	PLOTPX	\planes
	tst.w	%d5
	bmi.w	polydone\sfx
edge\sfx:
	lea	4(%a3),%a6		| next point, wrapping to the first
	cmp.l	%a4,%a6
	bcs.s	1f
	move.l	%a5,%a6
1:	move.w	(%a6),%d1
	sub.w	(%a3),%d1		| dx
	move.w	2(%a6),%d2
	sub.w	2(%a3),%d2		| dy
	movea.w	#160,%a1
	tst.w	%d2
	bpl.s	2f
	neg.w	%d2
	movea.w	#-160,%a1
2:	moveq	#0,%d4
	tst.w	%d1
	bpl.s	3f
	neg.w	%d1
	moveq	#1,%d4
3:	cmp.w	%d2,%d1
	bcs.w	ymajor\sfx
	move.w	%d1,%d7			| steps along x
	beq.w	edgedone\sfx		| no steps (a zero-length edge)
	move.w	%d1,%d3
	lsr.w	#1,%d3
	subq.w	#1,%d7
	tst.w	%d4
	bne.s	xleft\sfx
xright\sfx:
	sub.w	%d2,%d3
	bpl.s	4f
	add.w	%d1,%d3
	adda.w	%a1,%a0
4:	ror.w	#1,%d0
	bcc.s	5f
	addq.l	#8,%a0
5:	PLOTPX	\planes
	dbra	%d7,xright\sfx
	bra.s	edgedone\sfx
xleft\sfx:
	sub.w	%d2,%d3
	bpl.s	6f
	add.w	%d1,%d3
	adda.w	%a1,%a0
6:	rol.w	#1,%d0
	bcc.s	7f
	subq.l	#8,%a0
7:	PLOTPX	\planes
	dbra	%d7,xleft\sfx
	bra.s	edgedone\sfx
ymajor\sfx:
	move.w	%d2,%d7			| steps along y
	move.w	%d2,%d3
	lsr.w	#1,%d3
	subq.w	#1,%d7
	tst.w	%d4
	bne.s	yleft\sfx
yright\sfx:
	sub.w	%d1,%d3
	bpl.s	8f
	add.w	%d2,%d3
	ror.w	#1,%d0
	bcc.s	8f
	addq.l	#8,%a0
8:	adda.w	%a1,%a0
	PLOTPX	\planes
	dbra	%d7,yright\sfx
	bra.s	edgedone\sfx
yleft\sfx:
	sub.w	%d1,%d3
	bpl.s	9f
	add.w	%d2,%d3
	rol.w	#1,%d0
	bcc.s	9f
	subq.l	#8,%a0
9:	adda.w	%a1,%a0
	PLOTPX	\planes
	dbra	%d7,yleft\sfx
edgedone\sfx:
	addq.l	#4,%a3
	dbra	%d5,edge\sfx
polydone\sfx:
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
	.endm

	POLYMULTI 3, 3
	POLYMULTI 5, 5
	POLYMULTI 6, 6
	POLYMULTI 7, 7
	POLYMULTI 9, 9
	POLYMULTI 10, 10
	POLYMULTI 11, 11
	POLYMULTI 12, 12
	POLYMULTI 13, 13
	POLYMULTI 14, 14
	POLYMULTI 15, 15


| A rock outline from a prepared edge list (see build_rock_cache in platform_atari_st.c), all inside the screen.
| void st_draw_rock(unsigned char *buf, long cx, long cy, const short *cache, long plane_offset)
| cache: edges - 1 (-1 for none), x and y offset of the first corner, then per edge six words:
|   |dx|, |dy|, half the steps, steps - 1 (movem loads them into d1, d2, d3, d7), 160 or -160 (row step),
|   direction * 4 (0 x right, 1 x left, 2 y right, 3 y left).
| Same Bresenham as st_draw_polyline; only the per-edge arithmetic has been done in advance.
	.globl	_st_draw_rock
_st_draw_rock:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a2
	add.l	64(%sp),%a2		| a2 = buffer + plane offset
	move.l	60(%sp),%a3		| a3 = cache
	move.w	(%a3)+,%d5		| edges - 1
	move.w	(%a3)+,%d3
	add.w	54(%sp),%d3		| x of the first corner
	move.w	(%a3)+,%d6
	add.w	58(%sp),%d6		| y of the first corner
	mulu.w	#160,%d6
	lea	0(%a2,%d6.l),%a0
	move.w	%d3,%d6
	lsr.w	#4,%d6
	lsl.w	#3,%d6
	adda.w	%d6,%a0
	moveq	#15,%d6
	and.w	%d3,%d6
	move.w	#0x8000,%d0
	lsr.w	%d6,%d0			| pixel mask
	or.w	%d0,(%a0)
	tst.w	%d5
	bmi.w	rockdone
rockedge:
	movem.w	(%a3)+,%d1-%d3/%d7	| |dx|, |dy|, half, steps - 1
	movea.w	(%a3)+,%a1		| row step
	move.w	(%a3)+,%d6		| direction * 4
	jmp	rockjump(%pc,%d6.w)
rockjump:
	bra.w	rxright
	bra.w	rxleft
	bra.w	ryright
	bra.w	ryleft
rxright:
	sub.w	%d2,%d3
	bpl.s	1f
	add.w	%d1,%d3
	adda.w	%a1,%a0
1:	ror.w	#1,%d0
	bcc.s	2f
	addq.l	#8,%a0
2:	or.w	%d0,(%a0)
	dbra	%d7,rxright
	dbra	%d5,rockedge
	bra.s	rockdone
rxleft:
	sub.w	%d2,%d3
	bpl.s	3f
	add.w	%d1,%d3
	adda.w	%a1,%a0
3:	rol.w	#1,%d0
	bcc.s	4f
	subq.l	#8,%a0
4:	or.w	%d0,(%a0)
	dbra	%d7,rxleft
	dbra	%d5,rockedge
	bra.s	rockdone
ryright:
	sub.w	%d1,%d3
	bpl.s	5f
	add.w	%d2,%d3
	ror.w	#1,%d0
	bcc.s	5f
	addq.l	#8,%a0
5:	adda.w	%a1,%a0
	or.w	%d0,(%a0)
	dbra	%d7,ryright
	dbra	%d5,rockedge
	bra.s	rockdone
ryleft:
	sub.w	%d1,%d3
	bpl.s	6f
	add.w	%d2,%d3
	rol.w	#1,%d0
	bcc.s	6f
	subq.l	#8,%a0
6:	adda.w	%a1,%a0
	or.w	%d0,(%a0)
	dbra	%d7,ryleft
	dbra	%d5,rockedge
rockdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| Low-resolution 320x200, 4-plane line drawer.
| void st_draw_line_low(unsigned char *buf, long x0, long y0, long x1, long y1, long color)
	.macro	PUTPIXEL
	btst	#0,%d5
	beq.s	.Lp1\@
	or.w	%d0,(%a0)
.Lp1\@:	btst	#1,%d5
	beq.s	.Lp2\@
	or.w	%d0,2(%a0)
.Lp2\@:	btst	#2,%d5
	beq.s	.Lp3\@
	or.w	%d0,4(%a0)
.Lp3\@:	btst	#3,%d5
	beq.s	.Lp4\@
	or.w	%d0,6(%a0)
.Lp4\@:
	.endm

	.globl	_st_draw_line_low
_st_draw_line_low:
	movem.l	%d2-%d7/%a2,-(%sp)
	move.l	32(%sp),%a0
	move.l	36(%sp),%d6
	move.l	40(%sp),%d7
	move.l	44(%sp),%d1
	move.l	48(%sp),%d2
	move.l	52(%sp),%d5
	sub.l	%d6,%d1
	sub.l	%d7,%d2
	mulu.w	#160,%d7
	add.l	%d7,%a0
	move.l	%d6,%d3
	lsr.l	#4,%d3
	lsl.l	#3,%d3
	add.l	%d3,%a0
	moveq	#15,%d3
	and.l	%d6,%d3
	move.w	#0x8000,%d0
	lsr.w	%d3,%d0
	move.l	#160,%a1
	tst.l	%d2
	bpl.s	1f
	neg.l	%d2
	move.l	#-160,%a1
1:	moveq	#1,%d4
	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
	moveq	#-1,%d4
2:	cmp.l	%d2,%d1
	blt.s	5f
	move.l	%d1,%d3
	lsr.l	#1,%d3
	move.l	%d1,%d7
3:	PUTPIXEL
	sub.l	%d2,%d3
	bpl.s	4f
	add.l	%d1,%d3
	add.l	%a1,%a0
4:	tst.l	%d4
	bmi.s	6f
	lsr.w	#1,%d0
	bne.s	7f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	7f
6:	lsl.w	#1,%d0
	bne.s	7f
	move.w	#1,%d0
	subq.l	#8,%a0
7:	dbra	%d7,3b
	bra.s	10f
5:	move.l	%d2,%d3
	lsr.l	#1,%d3
	move.l	%d2,%d7
8:	PUTPIXEL
	sub.l	%d1,%d3
	bpl.s	9f
	add.l	%d2,%d3
	tst.l	%d4
	bmi.s	11f
	lsr.w	#1,%d0
	bne.s	9f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	9f
11:	lsl.w	#1,%d0
	bne.s	9f
	move.w	#1,%d0
	subq.l	#8,%a0
9:	add.l	%a1,%a0
	dbra	%d7,8b
10:	movem.l	(%sp)+,%d2-%d7/%a2
	rts

| Single pixels of one colour (the stars): every point inside the playing field is set to the colour, the
| others are skipped. Same result as st_plot_point for each point.
| void st_plot_points(unsigned char *buf, const short *points, long count, long color)
	.globl	_st_plot_points
_st_plot_points:
	movem.l	%d2-%d6/%a2,-(%sp)		| 24 bytes; arguments from 28(%sp)
	move.l	28(%sp),%a2
	move.l	32(%sp),%a1
	move.l	36(%sp),%d6
	ble.w	ppdone
	move.l	40(%sp),%d2
	subq.w	#1,%d6
pploop:
	move.w	(%a1)+,%d3			| x
	move.w	(%a1)+,%d4			| y
	move.w	%d3,%d0
	sub.w	#FIELD_X0,%d0
	cmp.w	#FIELD_W-1,%d0
	bhi.s	ppskip
	move.w	%d4,%d0
	sub.w	#FIELD_Y0,%d0
	cmp.w	#FIELD_H-1,%d0
	bhi.s	ppskip
	mulu.w	#160,%d4
	lea	0(%a2,%d4.l),%a0
	move.w	%d3,%d0
	lsr.w	#4,%d0
	lsl.w	#3,%d0
	adda.w	%d0,%a0				| the 16-pixel group
	moveq	#15,%d0
	and.w	%d3,%d0
	move.w	#0x8000,%d1
	lsr.w	%d0,%d1				| d1 = the pixel
	move.w	%d1,%d0
	not.w	%d0				| d0 = everything but the pixel
	btst	#0,%d2
	beq.s	1f
	or.w	%d1,(%a0)
	bra.s	2f
1:	and.w	%d0,(%a0)
2:	btst	#1,%d2
	beq.s	3f
	or.w	%d1,2(%a0)
	bra.s	4f
3:	and.w	%d0,2(%a0)
4:	btst	#2,%d2
	beq.s	5f
	or.w	%d1,4(%a0)
	bra.s	6f
5:	and.w	%d0,4(%a0)
6:	btst	#3,%d2
	beq.s	7f
	or.w	%d1,6(%a0)
	bra.s	ppskip
7:	and.w	%d0,6(%a0)
ppskip:
	dbra	%d6,pploop
ppdone:
	movem.l	(%sp)+,%d2-%d6/%a2
	rts

| Two neighbouring pixels (x, y) and (x + 1, y) in one bitplane: what a bullet is. Both must be on the screen.
| void st_draw_pair(unsigned char *buf, long x, long y, long plane_offset)
	.globl	_st_draw_pair
_st_draw_pair:
	move.l	8(%sp),%d0		| x
	move.l	12(%sp),%d1		| y
	move.l	4(%sp),%a0
	add.l	16(%sp),%a0		| + plane offset
	mulu.w	#160,%d1
	adda.l	%d1,%a0
	move.w	%d0,%d1
	lsr.w	#4,%d1
	lsl.w	#3,%d1
	adda.w	%d1,%a0			| the 16-pixel group of x
	and.w	#15,%d0
	cmp.w	#15,%d0
	beq.s	1f
	move.w	#0xc000,%d1
	lsr.w	%d0,%d1
	or.w	%d1,(%a0)
	rts
1:	or.w	#1,(%a0)		| the last pixel of this group ...
	or.w	#0x8000,8(%a0)		| ... and the first of the next
	rts

| Single-bitplane line: a two-point polyline.
| void st_draw_line_plane(unsigned char *buf, long x0, long y0, long x1, long y1, long plane_offset)
| plane_offset is the byte offset of the plane within a 16-pixel group: 0, 2, 4 or 6.
	.globl	_st_draw_line_plane
_st_draw_line_plane:
	subq.l	#8,%sp			| room for the two points
	move.w	18(%sp),(%sp)		| x0
	move.w	22(%sp),2(%sp)		| y0
	move.w	26(%sp),4(%sp)		| x1
	move.w	30(%sp),6(%sp)		| y1
	move.l	32(%sp),-(%sp)		| plane offset
	pea	2.w			| two points
	pea	8(%sp)			| the points (as they were before the two pushes above)
	move.l	24(%sp),-(%sp)		| buffer
	jsr	_st_draw_polyline
	lea	16(%sp),%sp
	addq.l	#8,%sp
	rts
