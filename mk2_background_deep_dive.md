# MK2 Backgrounds: BDD/BDB, Modules, Parallax, Floors and Animation

A deep dive into how Mortal Kombat II actually builds and runs a stage, read out of
the original sources in `mk2-readonly/mk2-main`, cross-checked against the 41 shipped
`.BDB`/`.BDD` pairs in `data/`, and turned into concrete guidance for bddtool.

Primary sources:

| File | What it is |
|---|---|
| `doc/load2/ldbgnd2.c` | LOAD2's `read_bgnd()` — the only authoritative BDD/BDB parser |
| `doc/load2/load2.h`, `wmpstruc.h` | All format limits (`MAXMODS`, `MAXBLOCKS`, …) |
| `doc/load2/load2.hlp` | `.LOD` directive reference (`BBB>`, `FRM>`, `PPP>`, `ZON>`, …) |
| `src/BAKGND.ASM` | Runtime block→display-list engine (`disp_mod`, `disp_add`, `addblock`) |
| `src/BGND.ASM` | Per-stage module tables, scroll tables, display lists, animators |
| `src/MKDISP.ASM` | `floor_code` — the skewed perspective floor |
| `src/MKUTIL.ASM` | `cycle_forward` / `cycle_backward` palette cycling |
| `src/MKBGANI.TBL`, `MKFLOORS.TBL` | LOAD2-generated tables for animation sprites and floors |
| `data/BGNDTEST.LOD`, `MK7MIL.LOD` | The build recipes that pull it all together |

Everything below is stated from those files. Where a claim is empirical it says so and
names the measurement.

---

## 1. The formats, exactly

### 1.1 BDB — the placement file (plain text)

```
<name> <xlength> <ylength> <zlength> <numMods> <numPals> <numBlocks>   ; map descriptor
<modname> <x1> <x2> <y1> <y2>                                          ; × numMods
<z:hex> <x> <y> <hdr:hex> <pal>                                        ; × numBlocks
```

Read by `ldbgnd2.c:757`:

```c
ct = fscanf (blk_file, "%s %u %u %u %u %u %u\n",
        name, &xlength, &ylength, &zlength, &numMods, &numPals, &numBlocks);
```

**`xlength`, `ylength` and `zlength` are read and never used again.** They appear
nowhere else in `ldbgnd2.c`. The world width/height/depth in the BDB header are pure
editor metadata — LOAD2 and the runtime derive everything from the module rectangles
and the block list. (bddtool's "Update Header" still has to keep `numMods`, `numPals`
and `numBlocks` correct, because those three *are* used, as bounds checks.)

Module line order matters (see §1.4). Block line order matters (see §2.4).

### 1.2 BDD — the pixel file (text headers + raw binary)

```
<num_hdr_ptrs>\n                         ; count of image data blocks
<oldh:hex> <w> <h> <big_pal>\n           ; × num_hdr_ptrs, each followed by
<w*h bytes of 8-bit indices>             ; raw, uncompressed, no padding
<palname> <palsize>\n                    ; × numPals, each followed by
<palsize × 2 bytes>                      ; RGB555 little-endian words
```

The palette name is parsed with `sscanf(buf80, "%s %u\n", pname, &palsize)` — a space
inside a palette name shifts every subsequent field and desynchronises the whole
palette table. bddtool already sanitises palette names for this reason; the same rule
applies to the stage name in the BDB descriptor, which is parsed with `%s`.

### 1.3 What LOAD2 emits

Per background file, into `bgndtbl.asm` / `bgndpal.asm` / `bgndequ.h`:

* `<NAME>HDRS:` — the image header table, **5 words (10 bytes) per image**:
  `.word w,h` / `.long address` / `.word dma_ctrl`.
  (The `/F` raw path writes 4 words instead; the game build uses the 5-word table —
  `BAKGND.ASM:437` indexes it as `(n<<6)+(n<<4)` = ×80 bits.)
* `<MODNAME>BLKS:` — one block table per module, **4 words (8 bytes) per block**:
  `.word z|0x40` / `.word x,y` (module-relative) / `.word hdr`.
* `<NAME>PALS:` — a `.long` pointer per palette.
* `W<mod>`/`H<mod>` equates in `BGNDEQU.H` — each module's pixel size.

Pixel data goes to image ROM at `0x2000000+`, deduplicated and bit-packed (§4).

### 1.4 How blocks get assigned to modules — the part that surprises people

`ldbgnd2.c:1010` onward:

1. A block belongs to a module if its **entire rectangle** is inside the module rect:
   `block.x >= mod.x1 && block.y >= mod.y1 && block.x+w-1 <= mod.x2 && block.y+h-1 <= mod.y2`.
   A block that straddles a module edge belongs to **no** module and is silently dropped
   from every block table.
2. Assignment is **exclusive and first-match-wins in module declaration order** —
   `bgnd_block[i].put` is set once. If two modules overlap and a block fits in both, the
   module listed **earlier in the BDB** takes it, and the later module never sees it.
3. Module rects are then **auto-shrunk to the bounding box of the blocks they captured**
   (`bgnd_mod[m].x1 = x1adj; …`). The rect you author is an upper bound / selection
   filter, not the module's final size. The shrunk size is what lands in `BGNDEQU.H`
   and what the runtime uses for placement and centering.
4. Block coordinates are rewritten **relative to the shrunk `x1,y1`**.

So a module is a *selection region plus a plane identity*, not a frame. Growing a module
rect into empty space costs nothing; overlapping two module rects quietly steals blocks.

### 1.5 Block word layout

`z` from the BDB is rewritten by LOAD2 before it is emitted:

```c
z = z & 0xFFF0;            /* keep upper 12 bits         */
z |= (pal_num & 0xF);      /* low nibble := palette low  */
bgnd_block[b].hdr += (pal_num >> 4) << 12;   /* palette high nibble into hdr 12-15 */
...
load_bits (bgnd_block[i].z | 0x40, 16);      /* transparency bit forced on, always */
```

| Field | Where it lives | Range |
|---|---|---|
| Draw priority (`z`) | block word 0, bits 8–15 | 0–255 |
| H-flip / V-flip | block word 0, bits 4/5 (`M_FLIPH 0x10`, `M_FLIPV 0x20`) | — |
| Transparency | bit 6 (`0x40`) — **set unconditionally by LOAD2** | — |
| Palette index | 4 low bits of word 0 + 4 bits at word 3 bits 12–15 | 0–255 |
| Image header index | word 3 bits 0–11 | 0–4095 (capped at 400 by `MAXHDRS`) |

The priority byte is applied at `BAKGND.ASM:564` as `srl 24,a3` — deliberately shifted so
the z value is **fractional and cannot affect the screen coordinate**. It is a sort key
for the display list and nothing else. It is not a scroll rate; scroll rate is a
per-plane property (§3).

---

## 2. Question 1 — Are there bounds on how many modules can be made?

Yes, four of them, and only the first is per-stage.

| Limit | Value | Defined in | Scope |
|---|---|---|---|
| `MAXMODS` | **100** | `load2.h:34` | modules in one BDB |
| `MAXBLKTBLS` | **500** | `load2.h:45` | *"MAX # of MODULES ACROSS all BGND FILES"* |
| `MAXHDRTBLS` | **50** | `load2.h:43` | background files in one `.LOD` |
| `MAXBLOCKS` | **2500** | `load2.h:36` | blocks in one BDB |
| `MAXHDRS` | **400** | `load2.h:37` | image headers in one BDB |
| `MAXPALS` | **256** | `load2.h:35` | palettes in one BDB |
| `MAXDATA` | **65500** | `load2.h:32` | pixels (= bytes) in one block |

Exceeding any of them calls `msgx()`, which prints and calls `graceful_exit(1)` — LOAD2
aborts the whole build, it does not degrade.

**But 100 is not the real ceiling.** Two tighter limits bite first:

1. **Eight planes.** `BAKGND.ASM:24-40` allocates exactly `bak1bits`…`bak8bits` and
   `bak1mods`…`bak8mods`, and `plane_info_table` (`BGND.ASM:135`) has 8 entries. A module
   only animates if a `<name>BMOD` is bound to one of `baklst1`…`baklst8` in the stage's
   `_mod` table. Modules beyond that are compiled into the ROM and never drawn.
2. **`init_bakmods` binds one module per plane.** The multi-module-per-plane path in
   `disp_mod` is commented out (`BGND.ASM:360-362`: `;***** (multiple module code)`).
   One module per plane, eight planes.

Empirically, across all 41 shipped BDBs the module count is **1–9**, and 9 (`KUNGFU2`)
includes one module (`CLOUDZ`) that captures **zero** blocks. The real working maximum
in the shipped game is 8.

> **Practical rule:** ≤ 8 modules per stage, one per parallax plane. 100 is the file
> format's limit; 8 is the engine's. Anything between the two loads fine and renders
> nothing.

The 500-modules-across-all-files and 50-files-per-`.LOD` budgets are project-wide and
bddtool does not currently model them (§8).

---

## 3. Question 2 — Parallax stacking defaults and bounds

### 3.1 The mechanism

Each plane `N` has its own world origin `worldtlxN` and its own scroll velocity
`scrollxN`. A stage supplies a **9-entry scroll table** (`BGND.ASM`, `<stage>_scroll`),
read as baklst index 8 down to 0, in 16.16 fixed point:

```
port_scroll
	.long	0		; 8
	.long	0		; 7
	.long	0		; 6 - sky
	.long	>8000		; 5 - little mountains
	.long	0		; 4
	.long	>10000		; 3 - house
	.long	>18000		; 2 - pillars
	.long	>20000		; 1 - main floor
	.long	0		; 0
```

**`>20000` = 2.0 px/frame is the reference rate — the plane the fighters stand on.**
A plane's parallax factor is `scroll[N] / 0x20000`. bddtool already encodes this
(`viewer_geometry.cpp:801`, `BDD_BGND_PLAYFIELD_SCROLL 0x20000`) and parses the real
table out of `BGND.ASM` rather than hardcoding it.

The whole table is then scaled per frame by the fighters' speed. `scroller`
(`BGND.ASM:200`) picks a multiplier from `right_scrolls`/`left_scrolls` based on the
faster player's `xvel`:

| Routine | Effective multiplier |
|---|---|
| `ss_10000` | ×0.5 (`sra 1`) |
| `ss_20000` | ×1.0 (table as-is) |
| `ss_28000` | ×1.25 |
| `ss_30000` | ×1.5 |
| `ss_40000` | ×2 |
| `ss_60000` | ×3 |
| `ss_70000` | ×3.5 |
| `ss_80000` | ×4 |
| `ss_a0000` | ×5 |

So the ratios between planes are fixed by the table; only the overall speed varies.

### 3.2 The real range, from every shipped stage

| Stage | Plane rates (baklst 1→8), as ×playfield |
|---|---|
| `port` | 1.0, 0.75, 0.5, —, 0.25, 0, 0, 0 |
| `arena` | 1.0, 0.8125, 0.625, 0.5, 0.25, 0.25, 0, 0 |
| `bridge` | 1.0, 1.0, 0.25, 0.125, 0.125, 0.0625, 0, 0 |
| `tomb` | 1.0, 0.75, 0.5, 0.5, 0, 0, 0, 0 |
| `dedpool` | 1.0, —, **1.5**, **1.25**, **1.125**, 0, 0, 0 |
| `battle` | 1.0, 0.8125, 0.5, 0.25, 0.09375, 0.0625, 0, 0 |
| `tower` | 1.0, —, 0.75, 0.6875, 0.625, 0.625, 0.625, 0.625 |
| `forest` | —, 1.0, 0.75, 0.75, 0.625, 0.5, 0.3125, 0 |
| `armory` | 1.0, 0.875, 0.65625, 0.65625, 0.375, 0, 0, 0.6875¹ |
| `cave` | **1.0625**, 1.0, 0.9375, 0.875, 0.75, 0.625, 0.5625, 0.5 |

¹ `armory` plane 8 carries no module — its rate exists only to drive the skewing floor (§4).

**Bounds and defaults that fall out of this:**

* **Bound:** 8 planes, one module each; `baklst1` is the back-most updated plane and the
  loop in `multi_plane` walks `baklst1` → `baklst8`.
* **Reference:** `0x20000` = the fighter plane. Always present.
* **Slowest non-zero:** `0x2000` = 0.0625× (`bridge`, `battle`). Below that, use 0 (locked).
* **Fastest:** `0x30000` = 1.5× (`dedpool` foreground chains). **Rates above 1.0 are
  legal and intentional** — that is how foreground elements are done. `cave` even puts
  1.0625× in front of the 1.0× playfield.
* **Typical stack:** 4–6 active planes. Only `cave` and `tower` use all 8.
* **Sky is 0**, not "very slow", in every stage that has one.
* A plane can share a rate with its neighbour (`bridge` 1 and 2 are both 1.0; `forest` 3
  and 4 are both 0.75) — planes are also a **draw-order** device, not only a speed device.

### 3.3 Placement offsets and draw order are separate from rate

Each `BMOD` entry in `<stage>_mod` is followed by `.word x,y` giving that plane's screen
placement offset (e.g. `DPUL5BMOD` / `.word 0,->58` raises the back wall by 0x58). Two
sentinels are supported (`BAKGND.ASM:304-328`): `>8000` = "start where the previous
module ended, minus this module's size", `>8001` = "start exactly where the previous
module ended" — relative chaining for tiled planes.

Draw order comes from a third table, `dlists_<stage>`, which is an explicit back-to-front
list and can interleave non-BDB layers:

```
dlists_port
	.long	baklst7,worldtlx7+16
	...
	.long	baklst2,worldtlx2+16
	.long	-1,floor_code		; the floor
	.long	-2			; player shadows
	.long	objlst,worldtlx+16	; fighters
	.long	objlst2,worldtlx+16
```

So **plane index, scroll rate and draw order are three independent knobs.** A common
mistake is assuming baklst order = draw order; it is `dlists_<stage>` that decides.

---

## 4. Question 3 — Parallax floors, including the ones not in the BDD/BDB

Seven MK2 stages have a floor that exists in **no** BDD or BDB file:

```
; MKFLOORS.TBL
FL_ARM	.set	03000000h
FL_TOW	.set	0306cfc0h
FL_TOMB	.set	030f0d20h
FL_FORST	.set	03158880h
FL_BATTL	.set	0319d160h
FL_PORT	.set	031ec300h
FL_ARENA	.set	0323b4a0h
```

### 4.1 How they are built

From `data/MK7MIL.LOD`:

```
***> 3000000,1
PPP> 6
ZOF>
ASM> MKFLOORS.TBL
FRM> FL_ARM
FRM> FL_TOW
...
```

* `FRM>` loads a **compressed movie-frame `.BIN` file**, not an `.IMG` sprite —
  `data/FL_ARM.BIN` (55,800 bytes), `FL_TOW.BIN` (67,500), etc.
* `PPP> 6` forces **6 bits per pixel** (64 colours).
* `ZOF>` disables zero-compression for them.
* They live in a separate ROM bank at `0x3000000`, away from the `0x2000000` block data.

### 4.2 How they are drawn

`floor_code` (`MKDISP.ASM:1997`) is invoked from the `dlists` slot `-1`. Per stage,
`<stage>_floor_info` supplies six values (`setup_floor_info`, `BGND.ASM:2942`):

```
armory_floor_info
	.long	FL_ARM			; source address in image ROM
	.long	MARFLOR			; palette
	.word	>c0			; skew_y   — screen Y the floor starts at
	.word	62			; skew_height — number of scanlines
	.long	scrollx8		; WHICH plane's scroll velocity drives it
	.long	armory_skew_calla	; per-frame slope routine
```

The draw loop (`MKDISP.ASM:2041-2061`) then, once per frame:

* takes `skew_oc` ("off centre"), advanced each frame by the chosen `scrollxN`;
* queues **one DMA per scanline**, each a `1 × 400` strip (`>00010190`) taken from a
  **1200-pixel-wide** source (`floor_x .set 1200`, stepping `floor_x*6` bits per line);
* adds `skew_dx` to the horizontal offset after every line, producing the perspective
  shear. `skew_dx` is recomputed each frame by `<stage>_skew_calla`, which subtracts
  `scroll >> 7` — that is what makes the MK2 floor appear to rotate as you scroll.

### 4.3 Why this matters for authoring

* **The floor is not a background plane.** It has no module, no blocks, no BDD images.
  It borrows a `scrollxN` slot purely as a velocity source — `armory` uses `scrollx8`,
  a plane with no `BMOD` at all, precisely so the floor's rate is independent of any
  visible plane. `port` shares `scrollx2` with the pillars.
* **Cost:** `skew_height` DMA queue entries per frame (42 for `port`, 62 for `armory`),
  drawn between the block planes and the shadows. They are DMA queue entries, not
  `getobj` display objects, so they do not consume the 358-object pool — but they are
  real per-frame DMA bandwidth.
* **A stage without a `floor_info` simply has no skew floor** and must paint its ground
  as ordinary blocks on the 1.0× plane (which is what `dedpool`, `kungfu*` and the
  non-fight screens do). Both approaches are valid; the skew floor buys you a
  perspective ground plane that blocks cannot express, at the cost of a fixed 1200×N
  6bpp asset in a separate ROM bank.
* bddtool already reads `<stage>_floor_info` out of `BGND.ASM` (`viewer_geometry.cpp:828-834`
  captures `floor_label`, `floor_palette`, `floor_y`, `floor_height`, `floor_skew_shift`),
  so the editor knows the floor exists — it just cannot author one (§8).

---

## 5. Question 4 — Does chopping sprites down too far hurt load and runtime?

Short answer: **yes, but not where most people expect, and moderate chopping actively
helps.** There are forces in both directions and they have different units.

### 5.1 Chopping *helps* on the load/ROM side — three mechanisms

1. **Per-block bit packing.** `compute_bpp()` (`ldbgnd2.c:69`) picks bits-per-pixel from
   the highest colour index *present in that block*:

   | max index | bpp |
   |---|---|
   | ≥128 | 8 |
   | ≥64 | 7 |
   | ≥32 | 6 |
   | ≥16 | 5 |
   | ≥8 | 4 |
   | ≥4 | 3 |
   | ≥2 | 2 |
   | else | 1 |

   Splitting a 200-colour image so that its flat/low-colour regions become separate
   blocks lets those regions drop from 8bpp to 4bpp or lower. That is a straight 2×+
   saving on the dominant cost.

2. **Checksum deduplication.** With `CON>` (the default), `srch_bd(w,h,checksum,dma2_field)`
   makes an identical block reuse the previous block's address and download **zero** new
   bytes. Chopping a repeating wall into repeated tiles collapses to one data copy plus
   N cheap header/block entries.

3. **Zero compression.** `ZON>` computes leading/trailing transparent runs per line.
   Chopping a sprite so transparent margins land in their own blocks lets whole blocks
   vanish. **Caveat:** `if (do_zcom && (w <= ZCOMPIXELS))` disables it for any block
   **≤10 pixels wide** (`load2.h:17`). Slivers narrower than that get no compression.

### 5.2 Chopping *hurts* on the runtime side — one mechanism, and it is hard

Every block that is on screen becomes **one display object from a single global pool**:

```
; DISPEQU.ASM:69-72
obsiz	.set	210h	; object block size
nobj	.set	358	; objects to display
```

**358 objects, game-wide.** `addblock` (`BAKGND.ASM:538`) calls `getobj`; on failure it
returns carry set and `disp_add` **stops adding blocks entirely** — the background
develops holes, and they appear at scene init when everything is being inserted at once.
That pool is shared with both fighters, all their projectiles and effects, stage actors,
UI and blood.

Blocks are added/removed with 48px of hysteresis on all four sides
(`disp_pad .set >00300030`), so the working window is **496 × 350**, not 400 × 254.

Measured on the shipped fight stages by `tools/analyze_mk2_backgrounds.py` (worst-case
count of blocks intersecting one 496×350 window, summed over every module in the BDB):

| Stage | Blocks | Peak on-screen | % of 358 | Widest block |
|---|---:|---:|---:|---:|
| `DEDPOOL` | 311 | 169 | 47% | 144 |
| `MK1CAVE` | 214 | 149 | 41% | 168 |
| `KUNGFU5` | 358 | 134 | 37% | 168 |
| `KUNGFU2` | 180 | 132 | 36% | 168 |
| `KUNGFU1` | 242 | 125 | 34% | 132 |
| `KUNGFU4` | 183 | 113 | 31% | 176 |
| `FOREST2` | 141 | 100 | 27% | 152 |
| `MOUNTAIN` | 106 | 82 | 22% | 172 |
| `ARENA` | 104 | 78 | 21% | 128 |
| `TOMB` | 220 | 73 | 20% | 144 |
| `ARMORY1` | 100 | 64 | 17% | 136 |
| `BRIDGE1` | 158 | 62 | 17% | 172 |
| `BATTLE` | 41 | 31 | 8% | 192 |

This counts every module in the file; the true runtime figure is lower where a stage
does not bind all of them. `DEDPOOL`'s BDB has 6 modules but `dedpool_mod` binds only 4
planes (`DPUL5`→baklst1, `DPUL2`→3, `DPUL3`→4, `DPUL4`→5; `DPUL1` and `DPUL6` are
commented out), giving ~149 actually drawn.

**Midway never went past ~47% of the pool on background alone**, and the peak fight-stage
figure is ~170. bddtool's `MK2_DISPLAY_OBJECT_RUNTIME_RESERVE = 56` is in the right
ballpark, but `MK2_DISPLAY_OBJECT_WARN = 300` is far above anything Midway shipped — the
practical background ceiling suggested by the data is **~180 simultaneous blocks**, and
a warn threshold near 200 would match observed practice much better than 300.

### 5.3 Two hard size bounds in the other direction

Chopping is not optional above a certain size:

* **`MAXDATA = 65500` pixels per block.** `w*h > 65500` aborts the build. That is
  ~256×256.
* **`widest_block .set 250`** (`BAKGND.ASM:51`). `disp_add` seeds its binary search at
  `disp_tl.x - 250`; a block wider than 250 px whose left edge is further left than that
  **is never found and never drawn**. Measured across all 41 shipped BDBs, the widest
  block anywhere is **244 px** (`NUPOOL`) — Midway respected this exactly. Max height
  seen is 232, max area 34,848.

### 5.4 The net rule

| Block size | Verdict |
|---|---|
| > 250 px wide | **Broken.** Will be skipped by `bsrch1stxb`. Must chop. |
| > 65,500 px area | **Build failure.** Must chop. |
| 120–244 px wide | The shipped sweet spot. |
| 32–120 px wide | Fine; use when it isolates colour regions or enables dedup. |
| ≤ 10 px wide | Zero-compression silently disabled. Avoid slivers. |
| Uniform small tiles | Cheap in ROM (dedup), **expensive at runtime** — each is an object. |

The cost of one extra block is: 8 bytes of block table + 10 bytes of header (if the
`w×h` is new) + **1 of 358 display objects and 1 DMA whenever it is visible**.
The saving is: whatever bpp reduction and dedup it buys.

> **Rule of thumb:** chop to isolate colour depth and to enable reuse, and never chop
> below the point where the extra blocks visible at once exceed ~150. Chopping a
> full-screen backdrop into 16×16 tiles puts ~680 tiles in the 496×350 window against a
> 358-object pool — it will not draw.

---

## 6. Question 5 — How to define background animations

MK2 has **three** distinct background animation techniques, none of which live in the
BDD/BDB.

### 6.1 Sprite animators — processes on a plane

Sprites come from ordinary `.IMG` files loaded through a `.LOD` into `MKBGANI.TBL`
(LOAD2's standard `IHDR SIZX:W,SIZY:W,ANIX:W,ANIY:W,SAG:L,CTRL:W,PAL:L` format):

```
warplite1:
	.word   13,13,6,5		; w, h, anim x, anim y
	.long   044f8e25H		; image address
	.word   02000H			; DMA control
	.long   lite99_p		; palette
```

The stage's `<stage>_calla` routine spawns one process per animation:

```
	create	pid_bani,warplite_animator
	create	pid_bani,lil_monk_animator
```

and the animator builds a display object, **inserts it into a specific plane's list**, and
walks a NULL-terminated frame table:

```
warplite_animator
	movi	warplite1,a5
	calla	gso_dmawnz_ns		; build object from image header
	movi	>008200c2,a4
	calla	set_xy_coordinates	; y:x
	move	a8,a0
	movi	baklst7,b4		; <-- plane it lives on
	calla	insobj_v
...
wla3	movi	a_warplite,a9
	movk	3,a0			; ticks per frame
	jsrp	framew			; play the sequence
	dsj	a11,wla3

a_warplite
	.long	warplite1
	.long	warplite2
	...
	.long	0			; terminator
```

Key points:

* **Inserting into `baklstN` is what gives the animation its parallax** — it inherits
  that plane's `worldtlxN`. `put_float` (`BGND.ASM:1424`) shows the manual variant:
  `move @worldtlx5+16,a5,w / add a5,a4` to place in plane-5 world space.
* `framew` with `a0` = ticks per frame; `sleep <n>` for pauses; `random_triple` for
  randomised sound.
* These objects come out of the **same 358-object pool** as background blocks. Budget
  them.

### 6.2 Palette cycling — zero object cost

`MKUTIL.ASM:2784`:

```
;  cycle_forward / cycle_backward
;  a10 = palette
;  a11 = [a,b,c,d]   a = starting color
;                    b = # of colors to cycle
;                    c = sleep time between each iteration
;                    d = unused
```

Used as:

```
armory_cycle
	sleep	4
	movi	VAT,a10
	movi	>2d0b0600,a11		; start at colour 0x2D, 11 colours, 6 ticks
	jauc	cycle_forward
```

spawned with `create pid_cycle,armory_cycle`. This is the lava shimmer. It costs **no
display objects and no DMA** — it rewrites palette RAM. It is by far the cheapest
animation available and is the right tool for lava, water, fire glow and neon.

### 6.3 RGB tables — pulsing a single colour

`rgb_stuff` (`MKUTIL.ASM:~2760`) walks a table of RGB words for one palette entry with a
per-step sleep, looping at an `end_stuff` terminator. Used for lightning flashes and
similar single-colour throbs.

### 6.4 Summary

| Technique | Where defined | Cost | Good for |
|---|---|---|---|
| Sprite animator | `.LOD` → `MKBGANI.TBL` + `create pid_bani,<proc>` in `BGND.ASM` | 1 object + 1 DMA per visible frame | Torches, bats, crowd, hanging bodies |
| Palette cycle | `create pid_cycle,<proc>` → `cycle_forward` | Free | Lava, water, glow, energy |
| RGB table | `rgb_stuff` | Free | Lightning, single-colour pulses |

**None of this is expressible in a BDD/BDB.** Any editor support has to emit or patch
`BGND.ASM` and the `.LOD`.

---

## 7. Question 6 — Features bddtool should add

Ranked by value, each grounded in something above. bddtool already models the LOAD2 caps
correctly (`bdd_core.h:17-34` matches `load2.h` exactly), so these are real gaps, not
restatements.

### 7.1 Validate `widest_block` (250 px) — **highest value, currently missing**

Nothing in `platform/` mentions 250 or `widest_block`. A block wider than 250 px is
silently dropped by `bsrch1stxb` at runtime; it validates clean, builds clean, and the
art is simply absent in MAME. Every shipped stage stays at or under 244 (zero violations
across all 41). This should be a hard error in `Mk2Diag`, alongside the existing
`load2_oversize_images`.

Note that bddtool's `Resize Sprite...`, `Composite PNG...` and `Tile Fill...` paths can
all produce a wider block, so the check belongs in the diagnostic rather than in any one
importer.

### 7.2 Enforce per-module ascending-X block order at save

**Measured:** all 41 shipped BDBs have **zero** X inversions within every module, and up
to 66 layer inversions — per-module ascending X is a hard invariant, layer order is not.
`bsrch1stxb` binary-searches the block table by X; violating it drops blocks.

Today bddtool reports `d->order_issues` as an "X-order caution" and offers a manual fix
in the LOAD2 Doctor. `bdd_core_save_bdb` writes blocks sorted by the `order` field, which
preserves file order for loaded stages (verified: a `--roundtrip-save` of `DEDPOOL`
keeps 0 inversions) — but any newly placed or horizontally moved object gets whatever
`order` it was assigned and can land out of sequence. This should be **automatic on
save**, not a caution. See §9.

### 7.3 Project-wide budgets (`MAXBLKTBLS` 500, `MAXHDRTBLS` 50)

bddtool validates one stage at a time. LOAD2 aborts the *whole build* if the modules
across every `BBB>` in the `.LOD` exceed 500, or the background file count exceeds 50. A
stage that passes alone can break the ROM build. A `.LOD`-level budget view — parse
`BGNDTEST.LOD`, sum modules across all referenced BDBs — would catch that.

### 7.4 Export palette cycles as MK2 assembly

`platform/UI/panels/pal_anim_panel.cpp` is **preview-only**: it holds up to 8 cycles as
`{pal_idx, lo, hi, hz, active}`, animates them live, and emits nothing. The runtime wants
`a10 = <palette label>`, `a11 = [start, count, sleep_ticks, 0]` and a
`create pid_cycle,<name>`. The parameters are one small conversion apart (`hz` →
ticks at 60 Hz; `lo`/`hi` → `start`/`count`). This is the cheapest animation in the
engine and the editor is one export button away from authoring it.

### 7.5 A parallax preview that uses the real speed multipliers

The editor reads `<stage>_scroll` and computes `scroll[N]/0x20000`. It does not model the
`ss_*` velocity multipliers (×0.5 … ×5) chosen per frame from fighter speed. A "walk
speed" slider on the game preview driving the same table would show the plane separation
players actually see, including the ×5 dash case where fast planes visibly outrun slow ones.

### 7.6 Module-overlap and straddle diagnostics

Two failure modes with no current check:

* Two module rects overlapping such that a block fits in both — the earlier module in
  BDB order silently takes it, the later never sees it.
* A block straddling a module edge — belongs to no module, dropped from every table.

bddtool has `unassigned_objects` (which covers the straddle case if `assign_module`
mirrors LOAD2's containment test) but nothing for overlap ambiguity, and it does not
show LOAD2's **auto-shrink**: the module rect you draw is not the module you get.
Drawing the shrunk rect alongside the authored one would make a whole class of "why did
my plane move" bugs obvious.

### 7.7 Floor authoring

Seven stages have a `floor_info`, and bddtool already parses it. Nothing can build one.
A tool that takes a 1200 px wide, 6bpp strip, previews the skew with the stage's real
`skew_y`/`skew_height`/`scrollxN`/`skew_calla` shift, and emits the `.BIN` + `MKFLOORS.TBL`
entry would close the last major "not in the BDD" gap. `midway-imgtool` in the workspace
is likely most of the encoder already.

### 7.8 Make the world-size header field honest

`xlength`/`ylength`/`zlength` are read by LOAD2 and never used. The editor treats world
size as meaningful. It should be labelled as editor-only metadata so nobody tunes it
expecting a runtime effect — while keeping `numMods`/`numPals`/`numBlocks` strictly synced,
because those *are* bounds-checked.

---

## 8. Question 7 — Features that are redundant or unhelpful

### 8.1 Too many doors into the same diagnostic

Six files consume `Mk2Diag` (`menu_bar.cpp`, `mk2_authoring_tools.cpp`,
`mk2_load2_doctor_tool.cpp`, `mk2_stage_readiness_gate.cpp`, `mk2_stage_recipe_manifest.cpp`,
`mk2_stage_wiring_check.cpp`), and the MK2 menu additionally offers *Stage Readiness Gate*,
*Finish-Line Gate*, *LOAD2 / Runtime Preview*, *Live MAME Preview Gate*, *ROM Preview Diff*,
*One-Click Validation Run*, *Visual Diagnostics*, *Integration Summary* and *Stage Preview
Dashboard*. Several print the same `X-order cautions` / `display object` numbers with
different labels. That is nine entry points to "is my stage OK?".

Consolidating to **one** validation panel with severity tiers (build-breaking / runtime /
advisory) and one "run the full gate" button would lose no capability and remove most of
the MK2 menu.

### 8.2 The `Simple Four-Image Level` / test-level generators

`New Full-Screen Proof Level`, `New Checker Test Level` and `Simple Four-Image Level` all
create throwaway projects with hardcoded bare paths (`BGPROF.BDB`, `CHECKER.BDB`,
`MK2SIMPLE.BDB` — `test_projects.cpp:235`, `mk2_level_start_helper_tool.cpp:237/357`).
They exist to exercise the pipeline, and the `--write-checker-test` / `--write-bg-proof`
CLI commands already cover that for the smoke suite. Two of the three could leave the
GUI entirely.

### 8.3 Grid Snap as a first-class control

Neither LOAD2 nor the runtime imposes any alignment on block X/Y — block coordinates are
arbitrary and get rewritten relative to the shrunk module origin anyway. Grid snap is a
convenience with no correctness meaning here, yet it sits in the View menu next to
settings that do have runtime consequences. *Visible Pixel Snap* is the one that
actually matters (it affects what dedups).

### 8.4 Depth/`zlength` in the New Project dialog

Every shipped stage has `depth = 255`, and LOAD2 discards the field. Asking for it on the
new-project form implies a choice that does not exist.

---

## 9. Question 8 — What should be on by default

### 9.1 Per-module X-sort on save — **on, always**

Per §7.2 this is a hard runtime invariant that every shipped file satisfies. It should
not be an opt-in fix in a doctor panel. Sort each module's blocks by ascending X at save
time and renumber `order` to match; the only thing that changes for an untouched file is
nothing (verified by round-trip), and hand-placed objects stop silently disappearing.

If sorting must remain opt-out, the toggle belongs in Preferences with a loud warning,
not buried behind a diagnostic.

### 9.2 `Show Module Bounds` — **on by default**

`platform/bg_editor.cpp:345` has `g_show_module_bounds = false`. Modules are the single
most consequential structure in the file: they define planes, they gate which blocks
exist at all, they get auto-shrunk, and a block one pixel outside one vanishes from the
build. Editing a stage with module bounds hidden is editing blind. `mk2_create_default_module`
already force-enables it (`mk2_analysis.cpp:299`) — which is a tell that the default is wrong.

### 9.3 Live display-object budget in the status bar

The 358-object pool is the constraint most likely to bite and the least visible.
`d->max_visible_objects` is already computed. It belongs on the status bar as a permanent
`blocks on screen: 149/358` readout, not behind *Level Stats*.

### 9.4 Advanced Mode

`g_simple_mode = true` by default (`bg_editor.cpp:348`). Simple mode does not remove much —
the MK2 menu stays — but it renames the domain vocabulary (*Modules* → *Regions*,
*Assign Layer (wx)* → *Assign Layer*) and drops *Save BDB + BDD*, *MK2 Stage Kit* and
*Sync MK2 Runtime Palettes* from the File and Tools menus. Given that "module" and "wx"
are exactly the concepts a person editing MK2 backgrounds has to learn — a module *is* a
parallax plane, `wx` *is* the priority/flip/palette word — renaming them teaches a model
that does not survive contact with `BGND.ASM`. Advanced should be the default, with
Simple offered as a deliberate choice.

---

## 10. Question 9 — What should be taken away

1. **The multi-document tab machinery, if unused for MK2 work.** Each document snapshots
   the entire project (`document_tabs.cpp:34-64`) and multiplies every save path
   (`save_all_dirty_documents` loops `save_all_project`, now once per unsaved document
   including its save-location prompt). A stage is edited against one `BGND.ASM` entry at
   a time; if nobody actually works two stages side by side, this is a large surface —
   and a large share of the save-path complexity — for little return.
2. **Two of the three test-level generators** (§8.2) — keep one, keep the CLI equivalents.
3. **Seven of the nine validation surfaces** (§8.1) — consolidate, do not delete the checks.
4. **`Depth` from the New Project dialog** (§8.4) — LOAD2 ignores it.
5. **The `Palette Animation` panel as-is** — either upgrade it to emit `cycle_forward`
   (§7.4) or drop it. A preview of an effect the editor cannot produce teaches the wrong
   mental model: it animates in Hz over a `lo..hi` range, while the engine animates in
   sleep-ticks over `start`+`count`.
6. **Bare-filename project paths** (`MK2STAGE.BDB`, `BGPROF.BDB`, `MK2START.BDB`,
   `CHECKER.BDB`) — these produced real saves into the executable's directory. Now fixed
   for the New Project path; `test_projects.cpp` and `mk2_level_start_helper_tool.cpp`
   still fabricate them as suggested names and should not fabricate paths at all.

---

## Appendix A — Constants worth pinning

| Constant | Value | Source |
|---|---|---|
| Screen | 400 × 254 (`scrrgt` 399, `scrbot` 253) | `DISPEQU.ASM:23,25` |
| Display object pool | **358** (`obsiz` 0x210 bits) | `DISPEQU.ASM:69-72` |
| Add/remove hysteresis | 48 px each side (`disp_pad >00300030`) | `BAKGND.ASM:50` |
| Widest safe block | **250 px** (`widest_block`) | `BAKGND.ASM:51` |
| Block table entry | 4 words / 8 bytes | `BAKGND.ASM:54-63` |
| Image header entry | 5 words / 10 bytes | `BAKGND.ASM:437` |
| Planes | 8 (`bak1`…`bak8`), one module each | `BAKGND.ASM:24-40`, `BGND.ASM:135` |
| Playfield scroll | `0x20000` = 2.0 px/frame | `BGND.ASM` `<stage>_scroll` |
| Speed multipliers | ×0.5 … ×5 | `BGND.ASM:386-404` |
| Floor source | 1200 px wide, 6 bpp, 400 px window | `MKDISP.ASM:1995`, `MK7MIL.LOD` |
| Modules / BDB | 100 | `load2.h:34` |
| Modules / project | 500 | `load2.h:45` |
| Background files / `.LOD` | 50 | `load2.h:43` |
| Blocks / BDB | 2500 | `load2.h:36` |
| Image headers / BDB | 400 | `load2.h:37` |
| Palettes / BDB | 256 | `load2.h:35` |
| Pixels / block | 65500 | `load2.h:32` |
| Zero-compression minimum width | 11 px (`ZCOMPIXELS` 10) | `load2.h:17` |

## Appendix B — Empirical survey of the 41 shipped stages

Widest block anywhere: **244** (`NUPOOL`). Tallest: **232**. Largest area: **34,848**
(`COMESOON`) — 53% of `MAXDATA`. Modules: 1–9, effectively 1–8. Blocks: 5–358. Images:
4–93. Palettes: 1–31. Blocks over `widest_block`: **0**. X-inversions within a module,
across every file: **0**.

Reproduce with:

```
python tools/analyze_mk2_backgrounds.py --mk2-root ../mk2-readonly/mk2-main --top 41
```

The `runtime_stats` / `module_blocks` / `peak_onscreen` helpers added to that script
replicate LOAD2's module claiming (full containment, first module in file order wins,
each block claimed once) and then sweep a 496 × 350 window per module. It reports peak
on-screen blocks against the 358-object pool, per-module X-order inversions, unassigned
(straddling) blocks, and any block wider than `widest_block`.
