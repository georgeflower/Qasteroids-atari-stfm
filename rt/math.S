| 68000-safe replacements for libgcc 32-bit mul/div (distro libgcc is 68020 code)
	.text
	.globl	__mulsi3, __udivsi3, __umodsi3, __divsi3, __modsi3

__mulsi3:
	move.w	4(%sp),%d0
	mulu.w	10(%sp),%d0
	move.w	8(%sp),%d1
	mulu.w	6(%sp),%d1
	add.w	%d1,%d0
	swap	%d0
	clr.w	%d0
	move.w	6(%sp),%d1
	mulu.w	10(%sp),%d1
	add.l	%d1,%d0
	rts

| core: d0 = dividend, d1 = divisor -> d0 = quotient, d1 = remainder
udivmod:
	move.l	%d2,-(%sp)
	move.l	%d3,-(%sp)
	moveq	#0,%d2
	tst.l	%d1
	beq.s	9f
	cmp.l	#0xffff,%d1		| fast path: 16-bit divisor, use DIVU twice
	bhi.s	5f
	move.l	%d0,%d3
	clr.w	%d3
	swap	%d3			| d3 = high word of dividend
	divu.w	%d1,%d3			| d3 = rem:quot(high)
	move.w	%d3,%d2			| d2 = high quotient
	move.w	%d0,%d3			| d3 = rem(high):low
	divu.w	%d1,%d3
	swap	%d2
	clr.w	%d2
	move.w	%d3,%d2			| d2 = full quotient
	swap	%d3
	moveq	#0,%d1
	move.w	%d3,%d1			| remainder
	move.l	%d2,%d0
	bra.s	9f
5:	moveq	#31,%d3
6:	add.l	%d0,%d0
	roxl.l	#1,%d2
	cmp.l	%d1,%d2
	bcs.s	7f
	sub.l	%d1,%d2
	addq.l	#1,%d0
7:	dbra	%d3,6b
	move.l	%d2,%d1
9:	move.l	(%sp)+,%d3
	move.l	(%sp)+,%d2
	rts

__udivsi3:
	move.l	4(%sp),%d0
	move.l	8(%sp),%d1
	bra.w	udivmod
__umodsi3:
	move.l	4(%sp),%d0
	move.l	8(%sp),%d1
	bsr.w	udivmod
	move.l	%d1,%d0
	rts
__divsi3:
	move.l	4(%sp),%d0
	move.l	8(%sp),%d1
	move.l	%d0,%a0
	move.l	%d1,%a1			| keep signs
	tst.l	%d0
	bpl.s	1f
	neg.l	%d0
1:	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
2:	bsr.w	udivmod
	move.l	%a0,%d1
	move.l	%a1,%a0
	cmp.l	#0,%a0
	bpl.s	3f
	not.l	%d1			| flip sign flag
3:	tst.l	%d1
	bpl.s	4f
	neg.l	%d0
4:	rts
__modsi3:
	move.l	4(%sp),%d0
	move.l	8(%sp),%d1
	move.l	%d0,%a0
	tst.l	%d0
	bpl.s	1f
	neg.l	%d0
1:	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
2:	bsr.w	udivmod
	move.l	%d1,%d0
	cmp.l	#0,%a0
	bpl.s	3f
	neg.l	%d0
3:	rts
