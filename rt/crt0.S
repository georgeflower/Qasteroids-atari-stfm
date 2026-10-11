| TOS startup for the Ubuntu m68k-linux-gnu-gcc build (see Makefile.ubuntu).
| Shrink the TPA to what the program needs plus its own stack, call main, Pterm.
	.text
	.globl	_start
_start:
	move.l	4(%sp),%a0		| basepage
	move.l	#0x100,%d0		| basepage size
	add.l	12(%a0),%d0		| + text
	add.l	20(%a0),%d0		| + data
	add.l	28(%a0),%d0		| + bss
	add.l	#STACK_BYTES,%d0	| + stack
	move.l	%a0,%d1
	add.l	%d0,%d1
	and.b	#0xfe,%d1
	move.l	%d1,%sp
	move.l	%d0,-(%sp)		| Mshrink(0, basepage, size)
	move.l	%a0,-(%sp)
	clr.w	-(%sp)
	move.w	#0x4a,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	jsr	main
	move.w	%d0,-(%sp)
	move.w	#0x4c,-(%sp)		| Pterm(code)
	trap	#1
