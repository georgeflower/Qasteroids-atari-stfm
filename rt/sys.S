| The few TOS calls the game makes (what <osbind.h> gives the MiNT toolchain), plus the GEMDOS file calls
| behind the small stdio replacement in libc.c. C arguments are 32-bit values on the stack. TOS may trash
| d2 and a2, which C expects to survive, so every wrapper saves them.
	.text

	.macro	SAVE
	movem.l	%d2/%a2,-(%sp)
	.endm
	.macro	RESTORE
	movem.l	(%sp)+,%d2/%a2
	.endm

	.globl	Super			| void *Super(void *ssp)  GEMDOS 0x20
Super:
	move.l	4(%sp),-(%sp)
	move.w	#0x20,-(%sp)
	trap	#1
	addq.l	#6,%sp
	rts

	.globl	Getrez			| int Getrez(void)  XBIOS 4
Getrez:
	SAVE
	move.w	#4,-(%sp)
	trap	#14
	addq.l	#2,%sp
	RESTORE
	ext.l	%d0
	rts

	.globl	Physbase		| void *Physbase(void)  XBIOS 2
Physbase:
	SAVE
	move.w	#2,-(%sp)
	trap	#14
	addq.l	#2,%sp
	RESTORE
	rts

	.globl	Logbase			| void *Logbase(void)  XBIOS 3
Logbase:
	SAVE
	move.w	#3,-(%sp)
	trap	#14
	addq.l	#2,%sp
	RESTORE
	rts

	.globl	Setscreen		| void Setscreen(void *log, void *phys, int rez)  XBIOS 5
Setscreen:
	SAVE
	move.l	20(%sp),%d0
	move.w	%d0,-(%sp)
	move.l	18(%sp),-(%sp)
	move.l	18(%sp),-(%sp)
	move.w	#5,-(%sp)
	trap	#14
	lea	12(%sp),%sp
	RESTORE
	rts

	.globl	Ikbdws			| void Ikbdws(int count, const char *ptr)  XBIOS 25 (count is bytes - 1)
Ikbdws:
	SAVE
	move.l	16(%sp),-(%sp)
	move.l	16(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#25,-(%sp)
	trap	#14
	addq.l	#8,%sp
	RESTORE
	rts

	.globl	Cconws			| long Cconws(const char *s)  GEMDOS 9
Cconws:
	SAVE
	move.l	12(%sp),-(%sp)
	move.w	#9,-(%sp)
	trap	#1
	addq.l	#6,%sp
	RESTORE
	rts

	.globl	st_f_open		| long st_f_open(const char *name, int mode)  GEMDOS 0x3d
st_f_open:
	SAVE
	move.l	16(%sp),%d0
	move.w	%d0,-(%sp)
	move.l	14(%sp),-(%sp)
	move.w	#0x3d,-(%sp)
	trap	#1
	addq.l	#8,%sp
	RESTORE
	rts

	.globl	st_f_create		| long st_f_create(const char *name)  GEMDOS 0x3c, attributes 0
st_f_create:
	SAVE
	clr.w	-(%sp)
	move.l	14(%sp),-(%sp)
	move.w	#0x3c,-(%sp)
	trap	#1
	addq.l	#8,%sp
	RESTORE
	rts

	.globl	st_f_read		| long st_f_read(int handle, long len, void *buf)  GEMDOS 0x3f
st_f_read:
	SAVE
	move.l	20(%sp),-(%sp)
	move.l	20(%sp),-(%sp)
	move.l	20(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x3f,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	RESTORE
	rts

	.globl	st_f_write		| long st_f_write(int handle, long len, const void *buf)  GEMDOS 0x40
st_f_write:
	SAVE
	move.l	20(%sp),-(%sp)
	move.l	20(%sp),-(%sp)
	move.l	20(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x40,-(%sp)
	trap	#1
	lea	12(%sp),%sp
	RESTORE
	rts

	.globl	st_f_close		| long st_f_close(int handle)  GEMDOS 0x3e
st_f_close:
	SAVE
	move.l	12(%sp),%d0
	move.w	%d0,-(%sp)
	move.w	#0x3e,-(%sp)
	trap	#1
	addq.l	#4,%sp
	RESTORE
	rts
