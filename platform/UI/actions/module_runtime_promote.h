#ifndef MODULE_RUNTIME_PROMOTE_H
#define MODULE_RUNTIME_PROMOTE_H

/* Promoting a module position to runtime.
 *
 * A module has two independent positions:
 *   builder  -- its rectangle in the BDB header, which is what LOAD2 uses to
 *               decide which objects belong to it, and what the world/builder
 *               canvas draws;
 *   runtime  -- the .word x,y after its *BMOD entry in BGND.ASM, which is where
 *               the game actually draws the plane.
 *
 * Moving a module on the builder grid changes only the first one. "Promote"
 * stamps the builder position onto the runtime placement so the two agree --
 * creating the *BMOD entry when the module has never been placed. This is the
 * one action that answers "I put it where I want it, now make the game use
 * that", and every entry point in the UI routes through here so they all
 * behave and report identically.
 *
 * Nothing here ever moves a BDB rectangle or an object, so promoting can never
 * change which module an object belongs to. */

struct ModuleRuntimeInfo {
    char name[64];
    bool valid;       /* module index parsed */
    bool placed;      /* has a *BMOD entry in BGND.ASM */
    int  builder_x;   /* module rectangle top-left (BDB source) */
    int  builder_y;
    int  runtime_x;   /* BGND.ASM placement, only meaningful when placed */
    int  runtime_y;
    int  drift_x;     /* runtime - builder; 0,0 when in sync */
    int  drift_y;
};

bool module_runtime_info(int module_idx, ModuleRuntimeInfo *out);

/* True when the module is placed and its runtime offset already equals its
 * builder rectangle's top-left -- i.e. there is nothing to promote. */
bool module_runtime_in_sync(int module_idx);

/* Stamp one module's builder position onto its runtime placement. Creates the
 * *BMOD entry when unplaced. Reports through stage_set_toast either way. */
bool module_promote_position_to_runtime(int module_idx);

/* Promote every module whose runtime placement differs from its builder
 * position. When include_unplaced is true, modules with no *BMOD entry get one
 * created at their builder position too. Returns the number of modules changed
 * and reports a summary toast. */
int module_promote_all_positions_to_runtime(bool include_unplaced,
                                            int *out_created, int *out_updated,
                                            int *out_failed);

/* How many modules would be affected: out_drifted = placed but out of sync,
 * out_not_placed = no *BMOD entry at all. Returns drifted + not_placed. */
int module_runtime_promote_pending(int *out_drifted, int *out_not_placed);

#endif
