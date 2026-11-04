| All the rocks in one loop: what game_render's draw_asteroid does for every rock, without going through C.
| Field offsets come from the compiler (build/asm_offsets.inc).
|
| void st_draw_rocks(GameState *state, unsigned char *buf, DirtyRect *rects, int *count, unsigned char *full,
|                    long max_rects, unsigned short **sprites)
|
| A rock with a shared outline (shape != 0) that lies inside the field is drawn by calling its prepared
| "sprite": a little generated routine per (size, shape, orientation, x offset within the 16-pixel group), a
| sequence of and.l / ori.l / move.l with immediate data for the shaded, four-bitplane rock, entered with a0 at
| the word of the rock's centre (see platform_build_rock_sprites). sprites is that table of routines, or NULL.
| Rocks that poke out of the field are drawn by platform_draw_rock_clipped when sprites is set.
|
| For every active rock: screen position from the world position (as screen_x/screen_y in game.c); bring the
| cached outline (game_prepare_rock) and the prepared edge list (platform_prepare_rock) up to date when the
| orientation changed; add the dirty rectangle (grown by a pixel and clamped to the field, like
| platform_mark_dirty; a rock wholly outside the field is skipped); then draw it: with st_draw_rock when the
| outline lies inside the field, or with st_draw_border_rock (runs of inside edges plus clipped edges, like
| platform_draw_polygon) when it pokes out. render_pending is cleared, so game_render leaves the rock alone.
#include "asm_offsets.inc"

	.text
	.globl	_st_draw_rocks
_st_draw_rocks:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 44 bytes; arguments start at 48(%sp)
	move.l	48(%sp),%a2			| a2 = state
	move.l	52(%sp),%a6			| a6 = screen buffer
	move.l	56(%sp),%a4			| a4 = dirty rectangle array
	move.l	60(%sp),%a5			| a5 = pointer to the rectangle count
	lea	GS_ASTEROIDS(%a2),%a3		| a3 = current rock
	moveq	#GA_MAX-1,%d7
rockloop:
	tst.b	GA_ACTIVE(%a3)
	beq	nextrock

	| screen position: field + ((world >> 11) * scale >> 13)
	move.l	GA_X(%a3),%d2
	asr.l	#8,%d2
	asr.l	#3,%d2
	muls.w	GS_X_SCALE(%a2),%d2
	asr.l	#8,%d2
	asr.l	#5,%d2
	add.w	GS_FIELD_X(%a2),%d2		| d2.w = centre x
	move.l	GA_Y(%a3),%d3
	asr.l	#8,%d3
	asr.l	#3,%d3
	muls.w	GS_Y_SCALE(%a2),%d3
	asr.l	#8,%d3
	asr.l	#5,%d3
	add.w	GS_FIELD_Y(%a2),%d3		| d3.w = centre y

	| cached outline for this orientation (angle >> 10), and its prepared edge list
	move.w	GA_ANGLE(%a3),%d4
	lsr.w	#8,%d4
	lsr.w	#2,%d4
	tst.b	GA_SHAPE(%a3)
	beq.s	1f
	and.w	#ROCK_ORIENT_MASK,%d4		| shared outlines turn in coarser steps
1:	tst.b	GA_CACHE_VALID(%a3)
	beq.s	prepare
	cmp.b	GA_CACHE_INDEX(%a3),%d4
	beq.s	prepared
prepare:
	move.l	%a3,-(%sp)
	move.l	%a2,-(%sp)
	jsr	_game_prepare_rock
	addq.l	#8,%sp
prepared:
	tst.b	GA_DRAW_CACHE_VALID(%a3)
	bne.s	edgesready
	move.l	%a3,-(%sp)
	jsr	_platform_prepare_rock
	addq.l	#4,%sp
edgesready:

	| the bounding box of the outline: d0 left, d5 top, d4 right, d6 bottom
	move.b	GA_BOUND_X0(%a3),%d0
	ext.w	%d0
	add.w	%d2,%d0
	move.b	GA_BOUND_Y0(%a3),%d5
	ext.w	%d5
	add.w	%d3,%d5
	move.b	GA_BOUND_X1(%a3),%d4
	ext.w	%d4
	add.w	%d2,%d4
	move.b	GA_BOUND_Y1(%a3),%d6
	ext.w	%d6
	add.w	%d3,%d6

	| wholly inside the field?
	cmp.w	GS_FIELD_X(%a2),%d0
	blt	borderrock
	cmp.w	GS_FIELD_Y(%a2),%d5
	blt	borderrock
	move.w	GS_FIELD_X(%a2),%d1
	add.w	GS_FIELD_W(%a2),%d1
	cmp.w	%d1,%d4
	bge	borderrock
	move.w	GS_FIELD_Y(%a2),%d1
	add.w	GS_FIELD_H(%a2),%d1
	cmp.w	%d1,%d6
	bge	borderrock

	bsr	appendrect
	tst.b	GA_SHAPE(%a3)
	beq	linedraw
	move.l	72(%sp),%d0
	beq	linedraw
	move.l	%d0,%a1
	moveq	#0,%d1
	move.b	GA_SIZE(%a3),%d1
	subq.w	#1,%d1
	.if ROCK_SHAPE_BITS
	lsl.w	#ROCK_SHAPE_BITS,%d1
	.endif
	moveq	#0,%d0
	move.b	GA_SHAPE(%a3),%d0
	subq.w	#1,%d0
	add.w	%d0,%d1				| (size - 1) * shapes + shape
	.if ROCK_ORIENT_BITS
	lsl.w	#ROCK_ORIENT_BITS,%d1
	move.w	GA_ANGLE(%a3),%d0
	lsr.w	#8,%d0
	lsr.w	#2,%d0
	and.w	#ROCK_ORIENT_MASK,%d0
	.if 6-ROCK_ORIENT_BITS
	lsr.w	#6-ROCK_ORIENT_BITS,%d0
	.endif
	add.w	%d0,%d1
	.endif
	lsl.w	#4,%d1
	move.w	%d2,%d0
	and.w	#15,%d0
	add.w	%d0,%d1				| ... * 16 + x offset within the group
	ext.l	%d1
	lsl.l	#2,%d1
	move.l	0(%a1,%d1.l),%d0
	beq	linedraw
	move.l	%d0,%a1
	move.w	%d3,%d0
	mulu.w	#160,%d0
	lea	0(%a6,%d0.l),%a0		| the row of the centre (the sprite writes all four bitplanes itself)
	move.w	%d2,%d0
	lsr.w	#4,%d0
	lsl.w	#3,%d0
	adda.w	%d0,%a0				| the 16-pixel group of the centre
	jsr	(%a1)
	bra	rockdone
linedraw:
	moveq	#0,%d0
	move.b	GA_SIZE(%a3),%d0
	neg.w	%d0
	addq.w	#4,%d0
	add.w	%d0,%d0				| plane offset (4 - size) * 2
	move.l	%d0,-(%sp)
	pea	GA_DRAW_CACHE(%a3)
	move.w	%d3,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| centre y
	move.w	%d2,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| centre x
	move.l	%a6,-(%sp)			| buffer
	jsr	_st_draw_rock
	lea	20(%sp),%sp
	bra	rockdone

borderrock:
	bsr	appendrect
	beq	rockdone			| nothing of it in the field
	tst.l	72(%sp)
	beq.s	outlineborder
	move.w	%d3,%d0				| shaded sprites: clipped through a scratch bitmap in C
	ext.l	%d0
	move.l	%d0,-(%sp)
	move.w	%d2,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)
	move.l	%a3,-(%sp)
	jsr	_platform_draw_rock_clipped
	lea	12(%sp),%sp
	bra	rockdone
outlineborder:
	| the corner points on the stack: centre + offsets
	lea	-48(%sp),%sp
	move.l	%sp,%a0
	moveq	#0,%d5
	move.b	GA_DRAW_COUNT(%a3),%d5		| number of corners
	move.w	%d5,%d6
	subq.w	#1,%d6
	moveq	#0,%d1
pointloop:
	move.b	GA_OFF_X(%a3,%d1.w),%d0
	ext.w	%d0
	add.w	%d2,%d0
	move.w	%d0,(%a0)+
	move.b	GA_OFF_Y(%a3,%d1.w),%d0
	ext.w	%d0
	add.w	%d3,%d0
	move.w	%d0,(%a0)+
	addq.w	#1,%d1
	dbra	%d6,pointloop
	| st_draw_border_rock(buf, points, count, plane, field x0, y0, x1, y1)
	move.w	GS_FIELD_Y(%a2),%d0
	add.w	GS_FIELD_H(%a2),%d0
	subq.w	#1,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| field y1
	move.w	GS_FIELD_X(%a2),%d0
	add.w	GS_FIELD_W(%a2),%d0
	subq.w	#1,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| field x1
	move.w	GS_FIELD_Y(%a2),%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| field y0
	move.w	GS_FIELD_X(%a2),%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| field x0
	moveq	#0,%d0
	move.b	GA_SIZE(%a3),%d0
	neg.w	%d0
	addq.w	#4,%d0
	add.w	%d0,%d0
	move.l	%d0,-(%sp)			| plane offset
	move.l	%d5,-(%sp)			| count
	pea	24(%sp)				| the points (just above the six longs pushed so far)
	move.l	%a6,-(%sp)			| buffer
	jsr	_st_draw_border_rock
	lea	80(%sp),%sp			| 8 arguments and the 48 bytes of points
rockdone:
	clr.b	GA_RENDER_PENDING(%a3)
nextrock:
	lea	GA_SIZEOF(%a3),%a3
	dbra	%d7,rockloop
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| Add the rock's dirty rectangle (d0 left, d5 top, d4 right, d6 bottom of its outline): one pixel bigger each
| way and clamped to the field, as platform_mark_dirty does. Z set on return when the rectangle is empty;
| otherwise it has been added, or the whole-field flag set when the list is full.
appendrect:
	subq.w	#1,%d0
	cmp.w	GS_FIELD_X(%a2),%d0
	bge.s	1f
	move.w	GS_FIELD_X(%a2),%d0
1:	subq.w	#1,%d5
	cmp.w	GS_FIELD_Y(%a2),%d5
	bge.s	2f
	move.w	GS_FIELD_Y(%a2),%d5
2:	addq.w	#1,%d4
	move.w	GS_FIELD_X(%a2),%d1
	add.w	GS_FIELD_W(%a2),%d1
	subq.w	#1,%d1				| last column of the field
	cmp.w	%d1,%d4
	ble.s	3f
	move.w	%d1,%d4
3:	addq.w	#1,%d6
	move.w	GS_FIELD_Y(%a2),%d1
	add.w	GS_FIELD_H(%a2),%d1
	subq.w	#1,%d1				| last row of the field
	cmp.w	%d1,%d6
	ble.s	4f
	move.w	%d1,%d6
4:	cmp.w	%d4,%d0
	bgt.s	rectempty
	cmp.w	%d6,%d5
	bgt.s	rectempty
	move.l	(%a5),%d1
	cmp.l	72(%sp),%d1			| room for another? (max_rects is at 68(%sp) in the loop, plus the return address)
	bge.s	rectfull
	lsl.l	#3,%d1
	lea	0(%a4,%d1.l),%a0
	move.w	%d0,(%a0)+
	move.w	%d5,(%a0)+
	move.w	%d4,(%a0)+
	move.w	%d6,(%a0)+
	addq.l	#1,(%a5)
	moveq	#1,%d1
	rts
rectfull:
	move.l	68(%sp),%a0
	move.b	#1,(%a0)			| too many rectangles: the whole field gets erased instead
	moveq	#1,%d1
	rts
rectempty:
	moveq	#0,%d1
	rts

| A polygon partly outside the field, in one bitplane: what platform_draw_polygon does for such a polygon.
| void st_draw_border_rock(unsigned char *buf, const short *points, long count, long plane_offset,
|                          long fx0, long fy0, long fx1, long fy1)
| If every corner is inside the field it is an ordinary closed polygon. Otherwise runs of consecutive edges
| that lie wholly inside are drawn with st_draw_polyline, and each other edge (and the closing edge) is
| clipped to the field (Cohen-Sutherland, in the same order as clip_line in platform_atari_st.c) and drawn.
	.globl	_st_draw_border_rock
_st_draw_border_rock:
	link.w	%a6,#-28			| 24 bytes of inside flags, a word for the clip loop
	movem.l	%d2-%d7/%a2-%a3,-(%sp)
	move.l	12(%a6),%a2			| points
	move.l	16(%a6),%d7			| count
	lea	-24(%a6),%a3			| flags
	move.l	%a2,%a0
	move.l	%a3,%a1
	move.w	34(%a6),%d3
	sub.w	26(%a6),%d3			| field width - 1
	move.w	38(%a6),%d4
	sub.w	30(%a6),%d4			| field height - 1
	moveq	#0,%d5				| corners outside
	move.w	%d7,%d6
	subq.w	#1,%d6
flagloop:
	move.w	(%a0)+,%d0
	sub.w	26(%a6),%d0
	cmp.w	%d3,%d0
	bhi.s	xoutside
	move.w	(%a0)+,%d1
	sub.w	30(%a6),%d1
	cmp.w	%d4,%d1
	bhi.s	youtside
	move.b	#1,(%a1)+
	dbra	%d6,flagloop
	bra.s	flagsdone
xoutside:
	addq.l	#2,%a0
youtside:
	clr.b	(%a1)+
	addq.w	#1,%d5
	dbra	%d6,flagloop
flagsdone:
	tst.w	%d5
	bne.s	partial
	move.l	20(%a6),-(%sp)			| every corner inside: a plain polygon
	move.l	%d7,-(%sp)
	move.l	%a2,-(%sp)
	move.l	8(%a6),-(%sp)
	jsr	_st_draw_poly_plane
	lea	16(%sp),%sp
	bra	borderdone
partial:
	moveq	#-1,%d5				| start of the current run of inside edges
	moveq	#0,%d4				| edge index
edgeloop:
	move.w	%d7,%d0
	subq.w	#1,%d0
	cmp.w	%d0,%d4
	bge	lastedge
	lea	0(%a3,%d4.w),%a0
	tst.b	(%a0)
	beq.s	notboth
	tst.b	1(%a0)
	beq.s	notboth
	tst.w	%d5
	bge.s	edgenext
	move.w	%d4,%d5				| a run starts here
	bra.s	edgenext
notboth:
	tst.w	%d5
	blt.s	oneedge
	move.w	%d4,%d0				| flush the run: points d5 .. d4
	sub.w	%d5,%d0
	addq.w	#1,%d0
	bsr	flushrun
	moveq	#-1,%d5
oneedge:
	move.w	%d4,%d1
	ext.l	%d1
	lsl.l	#2,%d1
	lea	0(%a2,%d1.l),%a0
	movem.w	(%a0),%d0-%d3			| x0, y0, x1, y1
	bsr	drawedge
edgenext:
	addq.w	#1,%d4
	bra.s	edgeloop
lastedge:
	tst.w	%d5
	blt.s	closing
	move.w	%d7,%d0				| the run that reaches the last corner
	sub.w	%d5,%d0
	bsr	flushrun
closing:
	move.w	%d7,%d1
	subq.w	#1,%d1
	ext.l	%d1
	lsl.l	#2,%d1
	lea	0(%a2,%d1.l),%a0
	move.w	(%a0),%d0
	move.w	2(%a0),%d1
	move.w	(%a2),%d2
	move.w	2(%a2),%d3
	bsr	drawedge
borderdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	unlk	%a6
	rts

| st_draw_polyline(buf, points + d5 * 4, d0 points, plane)
flushrun:
	ext.l	%d0
	move.w	%d5,%d1
	ext.l	%d1
	lsl.l	#2,%d1
	lea	0(%a2,%d1.l),%a0
	move.l	20(%a6),-(%sp)
	move.l	%d0,-(%sp)
	move.l	%a0,-(%sp)
	move.l	8(%a6),-(%sp)
	jsr	_st_draw_polyline
	lea	16(%sp),%sp
	rts

| The four Cohen-Sutherland region bits of (x, y) against the field: 1 left, 2 right, 4 above, 8 below.
	.macro	OUTCODE x, y, dest
	moveq	#0,\dest
	cmp.w	26(%a6),\x
	bge.s	1f
	moveq	#1,\dest
	bra.s	2f
1:	cmp.w	34(%a6),\x
	ble.s	2f
	moveq	#2,\dest
2:	cmp.w	30(%a6),\y
	bge.s	3f
	or.w	#4,\dest
	bra.s	4f
3:	cmp.w	38(%a6),\y
	ble.s	4f
	or.w	#8,\dest
4:
	.endm

| Clip the line (d0, d1) - (d2, d3) to the field and draw what is left. d0-d3 are words (sign-extended);
| d4-d7 are preserved, everything else is scratch.
drawedge:
	movem.l	%d4-%d7,-(%sp)
	OUTCODE	%d0,%d1,%d4			| d4 = code of the first end
	OUTCODE	%d2,%d3,%d5			| d5 = code of the second
	move.w	#8,-28(%a6)			| at most 8 clips
cliploop:
	move.w	%d4,%d6
	or.w	%d5,%d6
	beq	lineleft			| both ends inside: draw
	move.w	%d4,%d6
	and.w	%d5,%d6
	bne	edgegone			| both beyond the same side: nothing to draw
	move.w	%d4,%d6
	bne.s	havecode
	move.w	%d5,%d6
havecode:
	btst	#3,%d6
	bne.s	clipbottom
	btst	#2,%d6
	bne.s	cliptop
	btst	#1,%d6
	bne.s	clipright
	move.w	26(%a6),%d6			| left: x = field x0, y = y0 + (y1 - y0) * (x - x0) / (x1 - x0)
	sub.w	%d0,%d6
	move.w	%d3,%d7
	sub.w	%d1,%d7
	muls.w	%d6,%d7
	move.w	%d2,%d6
	sub.w	%d0,%d6
	divs.w	%d6,%d7
	add.w	%d1,%d7
	move.w	26(%a6),%d6
	bra.s	clipdone
clipright:
	move.w	34(%a6),%d6
	sub.w	%d0,%d6
	move.w	%d3,%d7
	sub.w	%d1,%d7
	muls.w	%d6,%d7
	move.w	%d2,%d6
	sub.w	%d0,%d6
	divs.w	%d6,%d7
	add.w	%d1,%d7
	move.w	34(%a6),%d6
	bra.s	clipdone
cliptop:
	move.w	30(%a6),%d7			| y = field y0, x = x0 + (x1 - x0) * (y - y0) / (y1 - y0)
	sub.w	%d1,%d7
	move.w	%d2,%d6
	sub.w	%d0,%d6
	muls.w	%d7,%d6
	move.w	%d3,%d7
	sub.w	%d1,%d7
	divs.w	%d7,%d6
	add.w	%d0,%d6
	move.w	30(%a6),%d7
	bra.s	clipdone
clipbottom:
	move.w	38(%a6),%d7
	sub.w	%d1,%d7
	move.w	%d2,%d6
	sub.w	%d0,%d6
	muls.w	%d7,%d6
	move.w	%d3,%d7
	sub.w	%d1,%d7
	divs.w	%d7,%d6
	add.w	%d0,%d6
	move.w	38(%a6),%d7
clipdone:					| new end point (d6, d7) replaces the outside end
	tst.w	%d4
	beq.s	movesecond
	move.w	%d6,%d0
	move.w	%d7,%d1
	OUTCODE	%d0,%d1,%d4
	bra.s	clipnext
movesecond:
	move.w	%d6,%d2
	move.w	%d7,%d3
	OUTCODE	%d2,%d3,%d5
clipnext:
	subq.w	#1,-28(%a6)
	bgt	cliploop
	bra.s	edgegone
lineleft:
	cmp.w	%d1,%d3				| a two-pixel horizontal run (a bullet) has its own routine, as in platform_draw_line
	bne.s	plainline
	move.w	%d2,%d6
	sub.w	%d0,%d6
	cmp.w	#1,%d6
	bne.s	plainline
	ext.l	%d0
	ext.l	%d1
	move.l	20(%a6),-(%sp)
	move.l	%d1,-(%sp)
	move.l	%d0,-(%sp)
	move.l	8(%a6),-(%sp)
	jsr	_st_draw_pair
	lea	16(%sp),%sp
	bra.s	edgegone
plainline:
	ext.l	%d0
	ext.l	%d1
	ext.l	%d2
	ext.l	%d3
	move.l	20(%a6),-(%sp)
	move.l	%d3,-(%sp)
	move.l	%d2,-(%sp)
	move.l	%d1,-(%sp)
	move.l	%d0,-(%sp)
	move.l	8(%a6),-(%sp)
	jsr	_st_draw_line_plane
	lea	24(%sp),%sp
edgegone:
	movem.l	(%sp)+,%d4-%d7
	rts
