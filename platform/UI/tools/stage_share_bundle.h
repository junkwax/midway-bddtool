#ifndef STAGE_SHARE_BUNDLE_H
#define STAGE_SHARE_BUNDLE_H

#include <stddef.h>

/* Repository that receives community stage submissions. Submissions arrive as
   issues (which accept file attachments); accepted stages are published to that
   repo's wiki, which cannot accept uploads itself. */
#define STAGE_SHARE_REPO "junkwax/midway-bddtool"
#define STAGE_SHARE_REPO_URL "https://github.com/" STAGE_SHARE_REPO

/* Credits carried with a shared stage. Persisted in settings so an author only
   types them once. */
extern char g_share_author[96];
extern char g_share_contact[96];      /* GitHub handle, no leading @ */
extern char g_share_description[512];
extern char g_share_credits[512];     /* source art, references, collaborators */
extern char g_share_license[96];
extern bool g_share_tested_mame;

/* Builds a shareable stage bundle in out_dir, laid out like a stage catalog
   entry: the arena renders, a scrolling animation, every prop sprite, and a
   wiki-ready page whose "How to enable" section is generated from the stage's
   real BGND.ASM bindings.

     <NAME>.BDB, <NAME>.BDD
     arena_layout.png   whole stage
     arena_game.png     in-game 400x254 framing
     scroll.gif         camera sweeping the stage and back
     scroll.mp4         same, far smaller (written when ffmpeg is available)
     props/NNN_*.png    every image in the BDD
     <NAME>.md          the page
     <NAME>-stage.zip   all of the above

   scroll_frames > 0 renders the animation (needs this executable to re-invoke
   itself, plus ffmpeg on PATH); 0 skips it. Returns files written, or -1. */
int stage_share_build_bundle(const char *out_dir, int scroll_frames,
                             char *zip_path, size_t zip_path_sz,
                             char *page_path, size_t page_path_sz,
                             char *err, size_t err_sz);

/* Builds the bundle, then offers the submission actions. File > Share Stage... */
void stage_share_submit_flow(void);

void draw_stage_share_dialog(void);
extern bool g_show_stage_share;

#endif
