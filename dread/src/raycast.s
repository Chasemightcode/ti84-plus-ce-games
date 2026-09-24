; DREAD raycaster core (eZ80, ADL mode).
;
; rc_cast(fb)  casts one ray per logical column (160) with a grid DDA and
;              writes a draw descriptor per column plus the depth buffer.
; rc_draw(fb)  fills ceiling/floor rows no wall touches, then draws every
;              wall column from its descriptor.
; rc_dup(fb)   copies each even screen row of the 3D view to the odd row
;              below it (the view is rendered at half vertical resolution).
;
; Formats: ray directions Q14 (from Q22 accumulators set up in render.c),
; DDA distances Q12 tiles, depth buffer Q8 tiles, texture coordinates 8.8.
;
; rc_cast and rc_draw use the shadow registers (and rc_draw moves SP), so
; both run with interrupts disabled and restore the previous state.

	.assume	adl = 1

VIEW_W		= 160
VIEW_H		= 100
HORIZON		= 50
ROW_BYTES	= 640
DESC_SIZE	= 14
UNROLL_BYTES	= 11		; bytes per unrolled pixel block in rc_draw
MIN_PERP	= 256		; 1/16 tile in Q12: caps wall height at 1920

; Column descriptor layout (DESC_SIZE bytes)
D_TEX		= 0		; 3: texture column pointer (page used for BC')
D_TP		= 3		; 2: HL' start: (column low byte + texel) << 8 | frac
D_STEP		= 5		; 2: DE' texture step, 8.8 texels per row
D_CMAP		= 7		; 1: colormap page high byte
D_DST		= 8		; 3: first destination byte
D_ENTRY		= 11		; 3: entry point into the unrolled loop

	.section	.bss._rc_state,"aw",@nobits
rc_cols:	.zero	VIEW_W * DESC_SIZE
rc_tmp:		.zero	4
rc_mtmp:	.zero	6
rc_fb:		.zero	3
rc_origin:	.zero	3
rc_zb:		.zero	3
rc_desc:	.zero	3
rc_xofs:	.zero	3
rc_rdx:		.zero	3
rc_rdy:		.zero	3
rc_ddx:		.zero	3
rc_ddy:		.zero	3
rc_sidex:	.zero	3
rc_sidey:	.zero	3
rc_stepx:	.zero	3
rc_stepy:	.zero	3
rc_perp:	.zero	3		; Q12
rc_perp8:	.zero	3		; Q8
rc_rowptr:	.zero	3
rc_saved_sp:	.zero	3
rc_fill_ret:	.zero	3
rc_h:		.zero	3
rc_step:	.zero	3
rc_fx:		.zero	1
rc_fy:		.zero	1
rc_sgnx:	.zero	1
rc_sgny:	.zero	1
rc_tile:	.zero	1
rc_side:	.zero	1
rc_usign:	.zero	1
rc_upos:	.zero	1
rc_flip:	.zero	1
rc_count:	.zero	1
rc_y0:		.zero	1
rc_n:		.zero	1

	.globl	_rc_min_height
_rc_min_height:	.zero	3

; ---------------------------------------------------------------------------
; rc_cast working state, addressed through IY (3-byte instructions).
V_TMP		= 0		; 4 scratch
V_DDX		= 4		; 3 grid-line spacing along the ray, Q12
V_DDY		= 7
V_SIDEX		= 10		; 3 distance to the first grid line, Q12
V_SIDEY		= 13
V_STEPX		= 16		; 3 map pointer step (+-1)
V_STEPY		= 19		; 3 map pointer step (+-64)
V_RDX		= 22		; 3 |ray dir|, Q14
V_RDY		= 25
V_PERP		= 28		; 3 perpendicular hit distance, Q12
V_PERP8		= 31		; 3 same in Q8
V_SGNX		= 34		; 1 0xFF if the ray points to -x
V_SGNY		= 35
V_FX		= 36		; 1 player position within its cell, Q8
V_FY		= 37
V_TILE		= 38		; 1 tile that was hit
V_SIDE		= 39		; 1 0 = crossed a vertical (x) grid line
V_COUNT		= 40		; 1 columns left
V_ORIGIN	= 41		; 3 map cell of the player
V_ZB		= 44		; 3 depth buffer write pointer
V_DST		= 47		; 3 fb + 2 * column
V_H		= 50		; 3 wall height
V_STEP		= 53		; 3 texture step
V_Y0		= 56		; 1 first row
V_N		= 57		; 1 row count
V_UOFS		= 58		; 1 texture offset along the wall (sliding doors)
V_SVHL		= 59		; 3 DDA registers saved while testing a door
V_SVDE		= 62
V_SVBC		= 65
V_SVIX		= 68
V_SVTILE	= 71		; 1
V_DOORP		= 72		; 3 -> doors[i]
V_DPLANE	= 75		; 3 distance to the door plane, Q12
V_SIZE		= 78

	.section	.bss._rc_vars,"aw",@nobits
rc_vars:	.zero	V_SIZE

; One axis of the per-column setup: |direction|, grid-line spacing
; (2^26 / |dir| through rc_recip_tab), map step and first-line distance.
	.macro	AXIS ray, dray, sgn, rd, dd, side, stepv, frac, mstep
	; current Q14 component = bits 8..23 of the Q22 accumulator
	ld	hl, (\ray + 1)
	ld	a, h
	rla
	sbc	a, a			; 0xFF if negative
	ld	(iy + \sgn), a
	jr	nc, .Lpos\@
	xor	a, a
	sub	a, l
	ld	l, a
	ld	a, 0
	sbc	a, h
	ld	h, a
.Lpos\@:
	inc	hl
	dec.sis	hl			; zero-extend the 16-bit magnitude
	ld	a, h
	or	a, a
	jr	nz, .Lbig\@
	ld	a, l
	cp	a, 129			; keep 2^26/|d| below 2^19
	jr	nc, .Lbig\@
	ld	l, 129
.Lbig\@:
	ld	(iy + \rd), hl
	; normalize into [2^14, 2^15), counting shifts in B
	ld	b, 0
.Lnorm\@:
	bit	6, h
	jr	nz, .Lnormed\@
	add	hl, hl
	inc	b
	jr	.Lnorm\@
.Lnormed\@:
	srl	h
	rr	l
	srl	h
	rr	l
	res	0, l			; 2 * ((dn - 16384) >> 3) + 4096
	ld	de, _rc_recip_tab - 4096
	add	hl, de
	ld	hl, (hl)
	inc	hl
	dec.sis	hl			; 16-bit mantissa ~ 2^27 / dn
	ld	a, b
	or	a, a
	jr	z, .Lhalf\@
	dec	b
	jr	z, .Lscaled\@
.Lshl\@:
	add	hl, hl
	djnz	.Lshl\@
	jr	.Lscaled\@
.Lhalf\@:
	srl	h
	rr	l
.Lscaled\@:
	ld	(iy + \dd), hl
	; advance the accumulator for the next column
	ld	de, (\ray)
	ld	bc, (\dray)
	ex	de, hl
	add	hl, bc
	ld	(\ray), hl
	; first grid-line distance = f * dd >> 8, f = frac (negative direction)
	; or 256 - frac (positive; frac 0 means a whole cell)
	ld	a, (iy + \sgn)
	or	a, a
	ld	a, (iy + \frac)
	jr	nz, .Lneg\@
	ld	hl, \mstep
	ld	(iy + \stepv), hl
	neg
	jr	nz, .Lmul\@
	ld	(iy + \side), de
	jr	.Ldone\@
.Lneg\@:
	ld	hl, -\mstep
	ld	(iy + \stepv), hl
.Lmul\@:
	; byte-wise (f * dd) >> 8 with dd = d2:d1:d0 (D, E hold d1, d0)
	ld	b, a
	ld	h, a
	ld	l, e
	mlt	hl			; f * d0
	ld	c, h
	ld	h, b
	ld	l, d
	mlt	hl			; f * d1
	ld	a, c
	add	a, l
	ld	(iy + \side), a
	ld	c, h
	ld	h, b
	ld	l, (iy + \dd + 2)
	mlt	hl			; f * d2
	ld	a, c
	adc	a, l
	ld	(iy + \side + 1), a
	ld	a, h
	adc	a, 0
	ld	(iy + \side + 2), a
.Ldone\@:
	.endm

; A = low byte of pos + perp8 * rd / 2^14: where along a wall a ray hits.
; In: HL = perp8 (Q8, 16-bit), DE = |rd| (Q14), C = 0xFF if rd < 0,
; B = position byte. Uses bits 6..13 of M = (perp8 * rd) >> 8, computed
; mod 2^16 from four 8x8 products: lo(pH*rH)<<8 + pH*rL + pL*rH + hi(pL*rL).
	.macro	WALLPOS
	push	bc
	ld	b, l
	ld	c, e
	mlt	bc			; pL*rL
	ld	a, b
	ld	b, h
	ld	c, e
	mlt	bc			; pH*rL
	push	bc
	ld	b, l
	ld	c, d
	mlt	bc			; pL*rH
	ld	l, d
	mlt	hl			; pH*rH
	ld	h, l
	ld	l, a
	add	hl, bc
	pop	bc
	add	hl, bc
	add	hl, hl
	add	hl, hl
	pop	bc			; B = position byte, C = sign
	ld	a, h
	xor	a, c			; negate when the direction is negative:
	sub	a, c			; (a ^ -1) + 1 = -a, (a ^ 0) - 0 = a
	add	a, b
	.endm

; HL = HL >> 8 using the stack (bits 8..23 in H:L; upper byte undefined).
; Only valid with interrupts disabled.
	.macro	SHR8_HL
	push	hl
	inc	sp
	pop	hl
	dec	sp
	.endm

; ---------------------------------------------------------------------------
; void rc_cast(uint8_t *fb)
	.section	.text._rc_cast,"ax",@progbits
	.globl	_rc_cast
_rc_cast:
	push	ix
	push	iy
	ld	hl, 9
	add	hl, sp
	ld	hl, (hl)
	ld	a, i			; P/V = interrupt enable state
	push	af
	di
	ld	iy, rc_vars
	ld	(iy + V_DST), hl

	; Player cell and fractional position.
	ld	hl, (_player)
	ld	(iy + V_FX), l
	ld	c, h			; cell x
	ld	hl, (_player + 3)
	ld	(iy + V_FY), l
	ld	l, h
	ld	h, 64
	mlt	hl			; cell y * 64
	ld	de, 0
	ld	e, c
	add	hl, de
	ld	de, _level_map
	add	hl, de
	ld	(iy + V_ORIGIN), hl

	ld	hl, _zbuffer
	ld	(iy + V_ZB), hl
	ld	hl, rc_cols
	ld	(rc_desc), hl
	ld	hl, 0xFFFF
	ld	(_rc_min_height), hl
	ld	(iy + V_COUNT), VIEW_W

.Lcol:
	AXIS	_rc_ray_x, _rc_ray_dx, V_SGNX, V_RDX, V_DDX, V_SIDEX, V_STEPX, V_FX, 1
	AXIS	_rc_ray_y, _rc_ray_dy, V_SGNY, V_RDY, V_DDY, V_SIDEY, V_STEPY, V_FY, 64

	; ---- DDA. IX = sideX, HL = sideX - sideY (its sign picks the axis:
	; adc/sbc on HL leave the S flag ready for the next branch), BC = ddx,
	; DE = ddy. Shadow set: HL' = map cell, BC' = x step, DE' = y step.
	exx
	ld	hl, (iy + V_ORIGIN)
	ld	bc, (iy + V_STEPX)
	ld	de, (iy + V_STEPY)
	exx
	ld	ix, (iy + V_SIDEX)
	ld	hl, (iy + V_SIDEX)
	ld	de, (iy + V_SIDEY)
	or	a, a
	sbc	hl, de
	ld	bc, (iy + V_DDX)
	ld	de, (iy + V_DDY)
	jp	p, .Lystep		; sideX >= sideY: y line first
.Lxstep:
	exx
	add	hl, bc
	ld	a, (hl)
	exx
	or	a, a			; also clears carry for adc
	jr	nz, .Lhit_x
.Lxcont:
	add	ix, bc
	adc	hl, bc
	jp	m, .Lxstep
.Lystep:
	exx
	add	hl, de
	ld	a, (hl)
	exx
	or	a, a			; also clears carry for sbc
	jr	nz, .Lhit_y
.Lycont:
	sbc	hl, de
	jp	p, .Lystep
	jr	.Lxstep

.Lhit_x:
	call	rc_mark
	bit	7, a
	jp	nz, .Ldoor_x
	ld	(iy + V_TILE), a
	ld	(iy + V_SIDE), 0
	exx				; the cell the ray came from
	or	a, a
	sbc	hl, bc
	ld	a, (hl)
	exx
	call	rc_jamb
	lea	hl, ix + 0		; perp = sideX
	jr	.Lhit_wall
.Lhit_y:
	call	rc_mark
	bit	7, a
	jp	nz, .Ldoor_y
	ld	(iy + V_TILE), a
	ld	(iy + V_SIDE), 1
	exx
	or	a, a
	sbc	hl, de
	ld	a, (hl)
	exx
	call	rc_jamb
	ex	de, hl			; DE = sideX - sideY
	lea	hl, ix + 0
	or	a, a
	sbc	hl, de			; perp = sideY
.Lhit_wall:
	ld	(iy + V_UOFS), 0
.Lhit:
	ld	de, MIN_PERP
	or	a, a
	sbc	hl, de
	add	hl, de
	jr	nc, .Lperp_ok
	ex	de, hl
.Lperp_ok:
	ld	(iy + V_PERP), hl
	; perp8 = perp >> 4 (Q8, < 65536) for depth, texture and light math
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	SHR8_HL
	inc	hl
	dec.sis	hl
	ld	(iy + V_PERP8), hl
	ex	de, hl
	ld	hl, (iy + V_ZB)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	(iy + V_ZB), hl

	; ---- wall position u = low byte of (pos + perp * dir / 2^14) along
	; the wall; texture flipped so it reads left to right from every side.
	; x faces: u from y, flip when looking -x. y faces: u from x, flip
	; when looking +y.
	ld	a, (iy + V_SIDE)
	or	a, a
	jr	nz, .Lu_yside
	ld	de, (iy + V_RDY)
	ld	c, (iy + V_SGNY)	; sign of the multiplier
	ld	a, (_player + 3)
	ld	b, a			; position byte
	ld	a, (iy + V_SGNX)	; flip mask (0xFF when looking -x)
	jr	.Lu_calc
.Lu_yside:
	ld	de, (iy + V_RDX)
	ld	c, (iy + V_SGNX)
	ld	a, (_player)
	ld	b, a
	ld	a, (iy + V_SGNY)
	cpl				; flip when looking +y
.Lu_calc:
	and	a, 31
	ld	(iy + V_TMP + 3), a	; xor mask for the column index
	ld	hl, (iy + V_PERP8)
	WALLPOS
	sub	a, (iy + V_UOFS)	; sliding doors: texture moves with the slab
	rrca
	rrca
	rrca
	and	a, 31
	xor	a, (iy + V_TMP + 3)
	ld	e, a			; texture column 0..31

	; ---- texture column pointer = rc_tile_tex[tile] + column * 32
	ld	l, (iy + V_TILE)
	ld	h, 3
	mlt	hl
	ld	bc, _rc_tile_tex
	add	hl, bc
	ld	hl, (hl)
	ld	d, 32
	mlt	de
	add	hl, de
	ld	ix, (rc_desc)
	ld	(ix + D_TEX), hl

	; ---- wall height = 491520 / perp (= 120 px * 4096 / perp, <= 1920).
	; A wall at least one tile away is under 128 px: 7 quotient bits do.
	ld	de, (iy + V_PERP)
	ld	bc, 0
	ld	a, (iy + V_PERP8 + 1)
	or	a, a
	jr	z, .Lh_near
	ld	hl, 3840		; 491520 >> 7
	.rept	7
	add	hl, hl
	sbc	hl, de
	jr	nc, . + 3
	add	hl, de
	rl	c
	.endr
	ld	a, c
	cpl
	and	a, 0x7F
	ld	c, a
	jr	.Lh_done
.Lh_near:
	ld	hl, 240			; 491520 >> 11
	.rept	11
	add	hl, hl
	sbc	hl, de
	jr	nc, . + 3
	add	hl, de
	rl	c
	rl	b
	.endr
	ld	a, c
	cpl
	ld	c, a
	ld	a, b
	cpl
	and	a, 7
	ld	b, a
.Lh_done:
	ld	(iy + V_H), bc

	; track the shortest wall for the ceiling/floor fill band
	ld	hl, (_rc_min_height)
	or	a, a
	sbc	hl, bc
	jr	c, .Lminh_ok
	ld	(_rc_min_height), bc
.Lminh_ok:

	; ---- texture step (8.8 texels per row) = perp8 * 4/15 ~ perp8*273 >> 10
	ld	hl, (iy + V_PERP8)
	push	hl
	pop	de			; zero-extended copy
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, de			; perp8 * 17
	ld	(iy + V_TMP + 1), de
	ld	(iy + V_TMP), 0
	ld	de, (iy + V_TMP)	; perp8 << 8
	add	hl, de			; perp8 * 273
	SHR8_HL
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(ix + D_STEP), l
	ld	(ix + D_STEP + 1), h
	ld	(iy + V_STEP), l

	; ---- vertical extent. Walls under 100 rows start at row 50 - h/2;
	; taller walls fill the view and skip h/2 - 50 rows of texture.
	ld	hl, 99
	or	a, a
	sbc	hl, bc
	jr	c, .Lclipped
	ld	a, c
	ld	(iy + V_N), a
	srl	a			; h/2
	cpl
	add	a, HORIZON + 1		; 50 - h/2
	ld	(iy + V_Y0), a
	ld	hl, 0			; texture starts at the top
	jr	.Lextent_done
.Lclipped:
	srl	b
	rr	c
	push	bc
	pop	hl
	ld	de, HORIZON
	or	a, a
	sbc	hl, de			; rows skipped above the view (<= 910)
	ld	(iy + V_Y0), 0
	ld	(iy + V_N), VIEW_H
	ex	de, hl
	ld	a, (iy + V_STEP)	; step < 82 here, fits in 8 bits
	ld	h, a
	ld	l, e
	mlt	hl
	ld	b, a
	ld	c, d
	mlt	bc
	ld	a, h
	add	a, c
	ld	h, a			; skipped * step, < 32 texels
.Lextent_done:
	; HL' start: texel address low byte in the high half
	ld	a, (ix + D_TEX)
	add	a, h
	ld	(ix + D_TP), l
	ld	(ix + D_TP + 1), a

	; ---- light: rc_light_tab[min(perp8 >> 5, 511)], x faces one darker
	ld	hl, (iy + V_PERP8)
	ld	a, h
	cp	a, 0x40
	jr	c, .Llight_idx
	ld	hl, 511
	jr	.Llight_look
.Llight_idx:
	add	hl, hl
	add	hl, hl
	add	hl, hl
	SHR8_HL
	inc	hl
	dec.sis	hl			; perp8 >> 5
.Llight_look:
	ld	de, _rc_light_tab
	add	hl, de
	ld	a, (iy + V_SIDE)
	xor	a, 1			; 1 for x faces
	add	a, (hl)
	ld	hl, _colormap
	ld	(iy + V_TMP), hl
	add	a, (iy + V_TMP + 1)	; colormap page for this light level
	ld	(ix + D_CMAP), a

	; ---- destination: fb + y0 * 640 + x * 2
	ld	l, (iy + V_Y0)
	ld	h, 3
	mlt	hl
	ld	de, _rc_row_ofs
	add	hl, de
	ld	hl, (hl)
	ld	de, (iy + V_DST)
	add	hl, de
	ld	(ix + D_DST), hl
	inc	de
	inc	de
	ld	(iy + V_DST), de

	; ---- unrolled-loop entry: rc_unroll_end - n * UNROLL_BYTES
	ld	h, (iy + V_N)
	ld	l, UNROLL_BYTES
	mlt	hl
	ex	de, hl
	ld	hl, rc_unroll_end
	or	a, a
	sbc	hl, de
	ld	(ix + D_ENTRY), hl

	; ---- next column
	lea	ix, ix + DESC_SIZE
	ld	(rc_desc), ix
	dec	(iy + V_COUNT)
	jp	nz, .Lcol

	pop	af
	jp	po, .Lcast_noei
	ei
.Lcast_noei:
	pop	iy
	pop	ix
	ret

; ---- a ray entered a door cell across a vertical (x) grid line.
; A = tile. Registers are the DDA's (see .Lxstep).
.Ldoor_x:
	ld	(iy + V_SVHL), hl
	ld	(iy + V_SVDE), de
	ld	(iy + V_SVBC), bc
	ld	(iy + V_SVIX), ix
	ld	(iy + V_SVTILE), a
	call	rc_door_ptr
	bit	1, a
	jr	nz, .Ldx_flush
	bit	0, a
	jr	z, .Ldx_pass		; E-W slab entered from its side: no plane here
	ld	hl, (iy + V_SVBC)	; the slab stands half a cell in: ddx / 2
	call	rc_half
	ex	de, hl
	; it is only reached if that comes before the next y line:
	; (sideX - sideY) + ddx/2 < 0
	ld	hl, (iy + V_SVHL)
	or	a, a
	adc	hl, de
	jp	p, .Ldx_pass		; the ray leaves through the recess wall
	ld	hl, (iy + V_SVIX)
	add	hl, de
	jr	.Ldx_plane
.Ldx_flush:
	ld	hl, (iy + V_SVIX)	; secret door: on the face the ray entered
.Ldx_plane:
	ld	(iy + V_DPLANE), hl
	call	rc_q12_to_q8
	ld	de, (iy + V_RDY)
	ld	c, (iy + V_SGNY)
	ld	a, (_player + 3)
	ld	b, a
	WALLPOS				; A = position along the slab
	ld	hl, (iy + V_DOORP)
	ld	c, (hl)			; open amount
	cp	a, c
	jr	c, .Ldx_pass		; through the open part
	inc	c
	jr	z, .Ldx_pass		; fully open
	dec	c
	ld	(iy + V_UOFS), c
	ld	a, (iy + V_SVTILE)
	ld	(iy + V_TILE), a
	ld	(iy + V_SIDE), 0
	ld	hl, (iy + V_DPLANE)
	jp	.Lhit
.Ldx_pass:
	ld	hl, (iy + V_SVHL)
	ld	de, (iy + V_SVDE)
	ld	bc, (iy + V_SVBC)
	ld	ix, (iy + V_SVIX)
	or	a, a
	jp	.Lxcont

; ---- a ray entered a door cell across a horizontal (y) grid line.
.Ldoor_y:
	ld	(iy + V_SVHL), hl
	ld	(iy + V_SVDE), de
	ld	(iy + V_SVBC), bc
	ld	(iy + V_SVIX), ix
	ld	(iy + V_SVTILE), a
	call	rc_door_ptr
	bit	1, a
	jr	nz, .Ldy_flush
	bit	0, a
	jr	nz, .Ldy_pass		; N-S slab entered from its side
	ld	hl, (iy + V_SVDE)	; ddy / 2
	call	rc_half
	ex	de, hl
	; reached before the next x line if ddy/2 - (sideX - sideY) < 0
	ld	hl, (iy + V_SVHL)
	ex	de, hl
	or	a, a
	sbc	hl, de
	jp	p, .Ldy_pass
	add	hl, de			; HL = ddy / 2 again
	ex	de, hl
	lea	hl, ix + 0		; sideY = sideX - (sideX - sideY)
	ld	bc, (iy + V_SVHL)
	or	a, a
	sbc	hl, bc
	add	hl, de
	jr	.Ldy_plane
.Ldy_flush:
	lea	hl, ix + 0
	ld	bc, (iy + V_SVHL)
	or	a, a
	sbc	hl, bc			; sideY
.Ldy_plane:
	ld	(iy + V_DPLANE), hl
	call	rc_q12_to_q8
	ld	de, (iy + V_RDX)
	ld	c, (iy + V_SGNX)
	ld	a, (_player)
	ld	b, a
	WALLPOS
	ld	hl, (iy + V_DOORP)
	ld	c, (hl)
	cp	a, c
	jr	c, .Ldy_pass
	inc	c
	jr	z, .Ldy_pass
	dec	c
	ld	(iy + V_UOFS), c
	ld	a, (iy + V_SVTILE)
	ld	(iy + V_TILE), a
	ld	(iy + V_SIDE), 1
	ld	hl, (iy + V_DPLANE)
	jp	.Lhit
.Ldy_pass:
	ld	hl, (iy + V_SVHL)
	ld	de, (iy + V_SVDE)
	ld	bc, (iy + V_SVBC)
	ld	ix, (iy + V_SVIX)
	or	a, a
	jp	.Lycont

; Marks the cell a ray just entered (HL' = its map address) as seen on the
; automap: rc_automap[cell] = 1. Preserves every register but the flags.
rc_mark:
	exx
	push	hl
	push	de
	ld	de, _level_map
	or	a, a
	sbc	hl, de
	ld	de, (_rc_automap)
	add	hl, de
	ld	(hl), 1
	pop	de
	pop	hl
	exx
	ret

; HL -> doors[A & 0x3F], A = its flags. Clobbers BC.
rc_door_ptr:
	and	a, 0x3F
	ld	l, a
	ld	h, 8
	mlt	hl
	ld	bc, _doors
	add	hl, bc
	ld	(iy + V_DOORP), hl
	inc	hl
	ld	a, (hl)
	dec	hl
	ret

; HL = HL / 2 for 0 <= HL < 2^23.
rc_half:
	ld	(iy + V_TMP), hl
	srl	(iy + V_TMP + 2)
	rr	(iy + V_TMP + 1)
	rr	(iy + V_TMP)
	ld	hl, (iy + V_TMP)
	ret

; HL = HL >> 4 (Q12 distance to Q8), zero-extended; HL < 2^20.
rc_q12_to_q8:
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	SHR8_HL
	inc	hl
	dec.sis	hl
	ret

; A = tile of the cell a ray left before hitting a wall. If that cell is a
; sliding door, the wall is the side of the doorway and gets the frame
; texture. Preserves HL and IX.
rc_jamb:
	bit	7, a
	ret	z
	push	hl
	and	a, 0x3F
	ld	l, a
	ld	h, 8
	mlt	hl
	ld	bc, _doors + 1
	add	hl, bc
	bit	1, (hl)			; secret doors have no frame
	pop	hl
	ret	nz
	ld	a, (_rc_jamb_tile)
	ld	(iy + V_TILE), a
	ret

; ---------------------------------------------------------------------------
; void rc_draw(uint8_t *fb)
	.section	.text._rc_draw,"ax",@progbits
	.globl	_rc_draw
_rc_draw:
	push	ix
	push	iy
	ld	hl, 9
	add	hl, sp
	ld	hl, (hl)
	ld	(rc_fb), hl
	ld	a, i			; P/V = interrupt enable state
	push	af
	di
	ld	(rc_saved_sp), sp

	; ---- band of rows covered by every wall: [50 - m/2, 50 + (m+1)/2)
	ld	hl, (_rc_min_height)
	ld	de, VIEW_H
	or	a, a
	sbc	hl, de
	add	hl, de
	jr	c, .Lband
	ld	hl, VIEW_H		; clamp: the band covers everything
.Lband:
	ld	a, l			; m <= 100
	srl	a
	ld	c, a			; m / 2
	ld	a, HORIZON
	sub	a, c
	ld	(rc_y0), a		; band top = ceiling rows to fill
	ld	a, l
	inc	a
	srl	a
	add	a, HORIZON
	ld	(rc_n), a		; band bottom = first floor row

	; ---- ceiling rows 0 .. band_top-1
	ld	a, (rc_y0)
	or	a, a
	jr	z, .Lceil_done
	ld	b, a
	ld	hl, (rc_fb)
	ld	(rc_rowptr), hl
	ld	iy, _rc_row_color
	call	rc_fill_rows
.Lceil_done:
	; ---- floor rows band_bottom .. 99
	ld	a, (rc_n)
	cp	a, VIEW_H
	jr	nc, .Lfloor_done
	ld	c, a
	ld	a, VIEW_H
	sub	a, c
	ld	b, a
	ld	l, c
	ld	h, 3
	mlt	hl
	ld	de, _rc_row_ofs
	add	hl, de
	ld	hl, (hl)
	ld	de, (rc_fb)
	add	hl, de
	ld	(rc_rowptr), hl
	ld	iy, _rc_row_color
	ld	de, 0
	ld	e, c
	add	iy, de
	call	rc_fill_rows
.Lfloor_done:

	; ---- wall columns
	ld	de, _colormap		; sets DEU; D is replaced per column
	ld	bc, ROW_BYTES - 1
	ld	ix, rc_cols
	ld	a, VIEW_W
	ld	(rc_count), a
.Ldraw_col:
	exx
	ld	bc, (ix + D_TEX)
	ld	hl, (ix + D_TP)
	ld	de, (ix + D_STEP)
	exx
	ld	d, (ix + D_CMAP)
	ld	hl, (ix + D_DST)
	ld	iy, (ix + D_ENTRY)
	jp	(iy)
	; One block per logical row: fetch texel, shade, write 2 pixels.
rc_unroll:
	.rept	VIEW_H
	exx
	ld	c, h
	ld	a, (bc)
	add	hl, de
	exx
	ld	e, a
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	(hl), a
	add	hl, bc
	.endr
rc_unroll_end:
	lea	ix, ix + DESC_SIZE
	ld	hl, rc_count
	dec	(hl)
	jp	nz, .Ldraw_col

	ld	sp, (rc_saved_sp)
	pop	af
	jp	po, .Ldraw_noei
	ei
.Ldraw_noei:
	pop	iy
	pop	ix
	ret

; Fill B rows (B >= 1) starting at rc_rowptr with colors from (IY),
; stepping one logical row (640 bytes) at a time. Uses SP as the write
; pointer: 107 pushes cover the 320-byte row plus one byte of the odd row
; below, which rc_dup overwrites later. Called with interrupts disabled.
rc_fill_rows:
	pop	hl
	ld	(rc_fill_ret), hl
.Lfill_row:
	ld	a, (iy)
	ld	(rc_tmp), a
	ld	(rc_tmp + 1), a
	ld	(rc_tmp + 2), a
	ld	hl, (rc_rowptr)
	ld	de, 321
	add	hl, de
	ld	sp, hl
	ld	hl, (rc_tmp)
	.rept	107
	push	hl
	.endr
	ld	hl, (rc_rowptr)
	ld	de, ROW_BYTES
	add	hl, de
	ld	(rc_rowptr), hl
	inc	iy
	dec	b
	jp	nz, .Lfill_row
	ld	sp, (rc_saved_sp)
	ld	hl, (rc_fill_ret)
	jp	(hl)

; ---------------------------------------------------------------------------
; void rc_sprite(const sprdesc_t *d): one scaled sprite, column by column,
; skipping columns where a wall is nearer and texels that are 0.
; sprdesc_t (render.h): tex, dst, zb, tx, step, tp0, depth, cols, rows, cmap.
;
; Magnified sprites (a texel at least two columns wide) are drawn two
; columns at a time from the texel column between them wherever both
; columns are in front of the wall: one texel fetch then fills 4 bytes.
SD_TEX		= 0		; 3 256-aligned column-major 32x32 texels
SD_DST		= 3		; 3 first pixel of the first visible column
SD_ZB		= 6		; 3 -> zbuffer[first visible column]
SD_TX		= 9		; 3 texture x of the first column, 8.8
SD_STEP		= 12		; 2 texels per logical pixel, 8.8 (both axes)
SD_TP0		= 14		; 2 texture y of the first row, 8.8
SD_DEPTH	= 16		; 2 Q8 distance
SD_COLS		= 18		; 1
SD_ROWS		= 19		; 1
SD_CMAP		= 20		; 1 colormap page
SPR_BLOCK	= 15		; bytes per unrolled pixel block
WIDE_BLOCK	= 21		; the same for a pair of columns

	.section	.bss._rc_spr,"aw",@nobits
spr_dst:	.zero	3
spr_tx:		.zero	3
spr_half:	.zero	3		; step / 2
spr_step2:	.zero	3		; step * 2
spr_cols:	.zero	1
spr_wide:	.zero	1		; 0xFF if magnified 2x or more

	.section	.text._rc_sprite,"ax",@progbits
	.globl	_rc_sprite
_rc_sprite:
	push	ix
	push	iy
	ld	hl, 9
	add	hl, sp
	ld	iy, (hl)
	ld	a, i
	push	af
	di
	; every column has the same height: patch the jumps into the blocks
	ld	h, (iy + SD_ROWS)
	ld	l, SPR_BLOCK
	mlt	hl
	ex	de, hl
	ld	hl, spr_unroll_end
	or	a, a
	sbc	hl, de
	ld	(.Lspr_jump + 1), hl
	ld	h, (iy + SD_ROWS)
	ld	l, WIDE_BLOCK
	mlt	hl
	ex	de, hl
	ld	hl, wide_unroll_end
	or	a, a
	sbc	hl, de
	ld	(.Lwide_jump + 1), hl
	; half and double step; wide when step <= 128
	ld	hl, 0
	ld	l, (iy + SD_STEP)
	ld	h, (iy + SD_STEP + 1)
	push	hl
	srl	h
	rr	l
	ld	(spr_half), hl
	pop	hl
	add	hl, hl
	ld	(spr_step2), hl
	ld	de, 257
	or	a, a
	sbc	hl, de
	sbc	a, a
	ld	(spr_wide), a
	ld	hl, (iy + SD_DST)
	ld	(spr_dst), hl
	ld	hl, (iy + SD_TX)
	ld	(spr_tx), hl
	ld	a, (iy + SD_COLS)
	ld	(spr_cols), a
	ld	ix, (iy + SD_ZB)
	ld	de, _colormap		; sets DEU; D = this sprite's light level
	ld	d, (iy + SD_CMAP)
	ld	bc, ROW_BYTES
.Lspr_col:
	exx
	ld	e, (iy + SD_DEPTH)
	ld	d, (iy + SD_DEPTH + 1)
	ld	hl, 0
	ld	l, (ix + 0)
	ld	h, (ix + 1)
	or	a, a
	sbc.sis	hl, de			; wall depth - sprite depth
	jr	c, .Lspr_hidden
	jr	z, .Lspr_hidden
	ld	a, (spr_wide)
	or	a, a
	jr	z, .Lspr_one
	ld	a, (spr_cols)
	dec	a
	jr	z, .Lspr_one
	ld	l, (ix + 2)		; is the next column visible too?
	ld	h, (ix + 3)
	or	a, a
	sbc.sis	hl, de
	jr	c, .Lspr_one
	jr	z, .Lspr_one
	ld	hl, (spr_tx)		; texel column between the two
	ld	de, (spr_half)
	add	hl, de
	ld	a, h
	call	spr_column
	exx
	ld	hl, (spr_dst)
.Lwide_jump:
	jp	0			; patched: entry into the wide blocks
.Lspr_one:
	ld	a, (spr_tx + 1)		; texture column 0..31
	call	spr_column
	exx
	ld	hl, (spr_dst)
.Lspr_jump:
	jp	0			; patched: entry into the blocks below
.Lspr_hidden:
	exx
	jp	spr_unroll_end
spr_unroll:
	.rept	VIEW_H
	exx
	ld	c, h
	ld	a, (bc)
	add	hl, de
	exx
	or	a, a
	jr	z, . + 8		; transparent
	ld	e, a
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	(hl), a
	dec	hl
	add	hl, bc
	.endr
spr_unroll_end:
	lea	ix, ix + 2
	ld	hl, (spr_dst)
	inc	hl
	inc	hl
	ld	(spr_dst), hl
	push	bc
	ld	hl, (spr_tx)
	ld	bc, (iy + SD_STEP)	; upper byte is junk; only bits 0..15 matter
	add	hl, bc
	ld	(spr_tx), hl
	pop	bc
	ld	hl, spr_cols
	dec	(hl)
	jp	nz, .Lspr_col
	jp	.Lspr_done
wide_unroll:
	.rept	VIEW_H
	exx
	ld	c, h
	ld	a, (bc)
	add	hl, de
	exx
	or	a, a
	jr	z, . + 14		; transparent
	ld	e, a
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a
	dec	hl
	dec	hl
	dec	hl
	add	hl, bc
	.endr
wide_unroll_end:
	lea	ix, ix + 4
	ld	hl, (spr_dst)
	inc	hl
	inc	hl
	inc	hl
	inc	hl
	ld	(spr_dst), hl
	push	bc
	ld	hl, (spr_tx)
	ld	bc, (spr_step2)
	add	hl, bc
	ld	(spr_tx), hl
	pop	bc
	ld	hl, spr_cols
	dec	(hl)
	dec	(hl)
	jp	nz, .Lspr_col
.Lspr_done:
	pop	af
	jp	po, .Lspr_noei
	ei
.Lspr_noei:
	pop	iy
	pop	ix
	ret

; Shadow-set setup for texture column A (0..31): BC' = the column (C is
; replaced per texel), H' = low byte of the first texel's address with the
; fraction in L', DE' = step. Called and returns with the shadow set active.
spr_column:
	ld	l, a
	ld	h, 32
	mlt	hl
	ld	bc, (iy + SD_TEX)
	add	hl, bc
	push	hl
	pop	bc
	ld	a, l
	ld	hl, (iy + SD_TP0)
	add	a, h
	ld	h, a
	ld	de, (iy + SD_STEP)
	ret

; ---------------------------------------------------------------------------
; void rc_gather(void): camera-space transform and view culling of things.
; For each active thing within GATHER_REACH of the player on both axes:
;   depth = (dx * cos + dy * sin) >> 9,  lat = (dy * cos - dx * sin) >> 9
; with _rc_c9/_rc_s9 = Q9 cos/sin (|.| <= 512) and dx, dy in Q8 tiles.
; Magnitudes and signs are split: the cos/sin magnitudes once per call,
; |dx| and |dy| once per thing, and each product is formed as
; (|a| * |b|) >> 8 in 16 bits from three 8x8 mlt (UMUL below), then signed
; and summed. Keeps things with depth >= GATHER_MIN and
; |lat| <= depth - depth/4 + half a tile (a little wider than the 67-degree
; view), writing 10-byte seen_t records {depth (2), lat (3), index (1),
; thing (3), pad (1)}.
GATHER_REACH	= 24 * 256		; monsters don't see farther either
GATHER_MIN	= 48
MAX_SEEN	= 32
T_X		= 0		; thing_t fields (dread.h)
T_Y		= 3
T_FLAGS		= 7

	.section	.bss._rc_gather,"aw",@nobits
g_adx:		.zero	3		; |dx|, |dy| (16-bit, zero-extended)
g_ady:		.zero	3
g_cmag:		.zero	3		; |cos|, |sin| (Q9, <= 512)
g_smag:		.zero	3
g_sdx:		.zero	1		; signs: 0 or 0xFF
g_sdy:		.zero	1
g_cneg:		.zero	1
g_sneg:		.zero	1
g_depth:	.zero	3
g_lat:		.zero	3
g_tmp:		.zero	3
g_out:		.zero	3
g_count:	.zero	1
g_index:	.zero	1

; HL = (|a| * |b| + 128) >> 8 for the 16-bit magnitudes stored at src and
; mag (|a| < 2^15, |b| <= 512, so the result is at most 15360): aL*bL
; contributes its rounded high byte, aH*bL + aL*bH is added whole and aH*bH
; one byte up.
; Only 16-bit (.sis) adds, so the result is exact and HLU = 0.
	.macro	UMUL src, mag
	ld	hl, (\src)		; L = aL, H = aH
	ld	bc, (\mag)		; C = bL, B = bH
	ld	d, l
	ld	e, c
	mlt	de			; aL*bL
	ld	a, e
	add	a, 128			; rounded: the high byte of aL*bL + 128
	ld	a, d
	adc	a, 0
	ld	d, h
	ld	e, c
	mlt	de			; aH*bL
	ld	c, a
	ld	a, h			; aH
	ld	h, b
	mlt	hl			; aL*bH (H = bH, L = aL)
	add.sis	hl, de
	ld	d, a
	ld	e, b
	mlt	de			; aH*bH (<= 60)
	ld	a, h
	add	a, e
	ld	h, a			; + aH*bH * 256
	ld	b, 0
	add.sis	hl, bc			; + (aL*bL) >> 8
	.endm

; HL = +-HL: negated when (sign1 ^ sign2) is 0xFF.
	.macro	SIGNED s1, s2
	ld	a, (\s1)
	ld	b, a
	ld	a, (\s2)
	xor	a, b
	call	nz, g_negate
	.endm

	.section	.text._rc_gather,"ax",@progbits
	.globl	_rc_gather
_rc_gather:
	push	ix
	ld	hl, (_rc_c9)
	call	g_abs16
	ld	(g_cmag), hl
	ld	(g_cneg), a
	ld	hl, (_rc_s9)
	call	g_abs16
	ld	(g_smag), hl
	ld	(g_sneg), a
	ld	ix, _things
	ld	hl, _rc_seen
	ld	(g_out), hl
	xor	a, a
	ld	(_rc_nseen), a
	ld	(g_index), a
	ld	a, (_num_things)
	or	a, a
	jp	z, .Lg_done
	ld	(g_count), a
.Lg_loop:
	bit	0, (ix + T_FLAGS)	; THING_ACTIVE
	jp	z, .Lg_next
	ld	hl, (ix + T_X)
	ld	de, (_player)
	call	rc_gdelta
	jp	nc, .Lg_next
	call	g_abs16
	ld	(g_adx), hl
	ld	(g_sdx), a
	ld	hl, (ix + T_Y)
	ld	de, (_player + 3)
	call	rc_gdelta
	jp	nc, .Lg_next
	call	g_abs16
	ld	(g_ady), hl
	ld	(g_sdy), a
	; behind the player for sure if neither product is positive: dx * c
	; is <= 0 when dx is 0 or its sign differs from cos's (same for y)
	ld	b, a
	ld	a, (g_sneg)
	xor	a, b			; 0xFF: dy * s < 0
	jr	nz, .Lg_ybehind
	ld	a, h
	or	a, l
	jr	nz, .Lg_front		; dy * s > 0 (or s = 0: checked below)
.Lg_ybehind:
	ld	a, (g_sdx)
	ld	b, a
	ld	a, (g_cneg)
	xor	a, b
	jp	nz, .Lg_next		; dx * c < 0 too
	ld	hl, (g_adx)
	ld	a, h
	or	a, l
	jp	z, .Lg_next		; dx = 0
.Lg_front:
	; depth = (dx * c + dy * s) >> 9
	UMUL	g_adx, g_cmag
	SIGNED	g_sdx, g_cneg
	push	hl
	UMUL	g_ady, g_smag
	SIGNED	g_sdy, g_sneg
	pop	de
	add	hl, de
	inc	hl			; rounded
	call	g_sar1
	ld	(g_depth), hl
	ld	de, GATHER_MIN
	or	a, a
	sbc	hl, de
	jp	m, .Lg_next		; behind the player or too close
	; lat = (dy * c - dx * s) >> 9
	UMUL	g_ady, g_cmag
	SIGNED	g_sdy, g_cneg
	push	hl
	UMUL	g_adx, g_smag
	SIGNED	g_sdx, g_sneg
	pop	de
	ex	de, hl
	or	a, a
	sbc	hl, de
	inc	hl			; rounded
	call	g_sar1
	ld	(g_lat), hl
	; limit = depth - depth/4 + 128
	ld	hl, (g_depth)		; 48 <= depth < 2^16, upper byte 0
	srl	h
	rr	l
	srl	h
	rr	l
	ex	de, hl
	ld	hl, (g_depth)
	or	a, a
	sbc	hl, de
	ld	de, TILE_HALF
	add	hl, de
	ex	de, hl			; DE = limit
	ld	hl, (g_lat)
	bit	7, h
	jr	z, .Lg_lat_pos
	ex	de, hl
	or	a, a
	adc	hl, de			; limit - |lat|
	jp	m, .Lg_next
	jr	.Lg_emit
.Lg_lat_pos:
	ex	de, hl
	or	a, a
	sbc	hl, de			; limit - lat
	jp	m, .Lg_next
.Lg_emit:
	ld	hl, (g_out)
	ld	de, (g_depth)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	de, (g_lat)
	ld	(hl), de
	inc	hl
	inc	hl
	inc	hl
	ld	a, (g_index)
	ld	(hl), a
	inc	hl
	ld	(hl), ix		; -> the thing itself
	ld	de, 4
	add	hl, de
	ld	(g_out), hl
	ld	hl, _rc_nseen
	inc	(hl)
	ld	a, (hl)
	cp	a, MAX_SEEN
	jr	z, .Lg_done		; list full
.Lg_next:
	ld	de, (_rc_thing_stride)
	add	ix, de
	ld	hl, g_index
	inc	(hl)
	ld	hl, g_count
	dec	(hl)
	jp	nz, .Lg_loop
.Lg_done:
	pop	ix
	ret

TILE_HALF	= 128

; HL = HL - DE, carry set if -GATHER_REACH < HL < GATHER_REACH.
rc_gdelta:
	or	a, a
	sbc	hl, de
	push	hl
	ld	de, GATHER_REACH
	add	hl, de
	ld	de, 2 * GATHER_REACH
	or	a, a
	sbc	hl, de
	pop	hl
	ret

; HL = |HL| zero-extended, A = 0xFF if HL was negative (else 0), for a
; sign-extended 24-bit HL with |HL| < 2^15.
g_abs16:
	xor	a, a
	bit	7, h
	jr	z, .Lab_pos
	ex	de, hl
	sbc	hl, hl			; carry is clear after xor: HL = 0
	sbc	hl, de
	dec	a
.Lab_pos:
	inc	hl
	dec.sis	hl
	ret

; HL = -HL (24-bit).
g_negate:
	ex	de, hl
	or	a, a
	sbc	hl, hl
	sbc	hl, de
	ret

; HL = HL >> 1, arithmetic, for a 24-bit signed HL.
g_sar1:
	ld	(g_tmp), hl
	ld	hl, g_tmp + 2
	sra	(hl)
	dec	hl
	rr	(hl)
	dec	hl
	rr	(hl)
	ld	hl, (g_tmp)
	ret

; ---------------------------------------------------------------------------
; void rc_weapon(uint8_t *fb, const gfx_rletsprite_t *spr, int x, int y)
; Draws a convimg RLET sprite at logical (x, y), 2 bytes per texel into the
; even screen rows, and stops at the bottom of the view (rc_dup doubles the
; rows afterwards). RLET rows: [transparent run][opaque run][texels]...,
; ending as soon as the width is reached; an opaque run is never 0.
; The caller keeps 0 <= x <= 160 - width and 0 <= y < 100.
	.section	.bss._rc_weapon,"aw",@nobits
wp_w:		.zero	1
wp_rows:	.zero	1

	.section	.text._rc_weapon,"ax",@progbits
	.globl	_rc_weapon
_rc_weapon:
	push	ix
	ld	ix, 0
	add	ix, sp
	ld	de, (ix + 9)		; DE = sprite
	ld	a, (de)
	ld	(wp_w), a
	inc	de
	ld	a, (de)			; height
	inc	de
	ld	c, a
	ld	a, VIEW_H
	sub	a, (ix + 15)		; rows left below y
	jr	c, .Lw_done
	jr	z, .Lw_done
	cp	a, c
	jr	c, .Lw_rows
	ld	a, c
.Lw_rows:
	ld	(wp_rows), a
	ld	l, (ix + 15)		; HL = fb + y * 640 + x * 2
	ld	h, 3
	mlt	hl
	ld	bc, _rc_row_ofs
	add	hl, bc
	ld	hl, (hl)
	ld	bc, (ix + 6)
	add	hl, bc
	ld	bc, (ix + 12)
	add	hl, bc
	add	hl, bc
.Lw_row:
	push	hl			; row start
	ld	a, (wp_w)
	ld	c, a			; C = texels left in this row
.Lw_seg:
	ld	a, (de)			; transparent run
	inc	de
	or	a, a
	jr	z, .Lw_opaque
	push	de
	ld	de, 0
	ld	e, a
	add	hl, de
	add	hl, de
	pop	de
	ld	b, a
	ld	a, c
	sub	a, b
	ld	c, a
	jr	z, .Lw_row_end
.Lw_opaque:
	ld	a, (de)			; opaque run
	inc	de
	ld	b, a
	ld	a, c
	sub	a, b
	ld	c, a
.Lw_copy:
	ld	a, (de)
	inc	de
	ld	(hl), a
	inc	hl
	ld	(hl), a
	inc	hl
	djnz	.Lw_copy
	ld	a, c
	or	a, a
	jr	nz, .Lw_seg
.Lw_row_end:
	pop	hl
	ld	bc, ROW_BYTES
	add	hl, bc
	ld	a, (wp_rows)
	dec	a
	ld	(wp_rows), a
	jr	nz, .Lw_row
.Lw_done:
	pop	ix
	ret

; ---------------------------------------------------------------------------
; void rc_dup(uint8_t *fb): copy even rows 0,2,..,198 down one row.
	.section	.text._rc_dup,"ax",@progbits
	.globl	_rc_dup
_rc_dup:
	ld	hl, 3
	add	hl, sp
	ld	hl, (hl)
	ld	a, VIEW_H
.Ldup_row:
	push	hl
	pop	de
	ld	bc, 320
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ldir
	ld	bc, 320
	add	hl, bc
	dec	a
	jr	nz, .Ldup_row
	ret

	.extern	_player
	.extern	_level_map
	.extern	_zbuffer
	.extern	_colormap
	.extern	_rc_tile_tex
	.extern	_rc_light_tab
	.extern	_rc_row_color
	.extern	_rc_row_ofs
	.extern	_rc_ray_x
	.extern	_rc_ray_y
	.extern	_rc_ray_dx
	.extern	_rc_ray_dy
	.extern	_rc_recip_tab
	.extern	_doors
	.extern	_things
	.extern	_num_things
	.extern	_rc_seen
	.extern	_rc_nseen
	.extern	_rc_c9
	.extern	_rc_s9
	.extern	_rc_thing_stride
	.extern	_rc_jamb_tile
	.extern	_rc_automap
