#include "bg_editor_globals.h"
#include "bg_editor.h"
#include "Core/editor_project_storage.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>

#define MAX_RUNTIME_ACTORS 64
#define MAX_RUNTIME_ACTOR_FRAMES 24
#define MAX_INFERRED_RUNTIME_GROUPS 128

struct RuntimeStageActor {
    char name[64];
    char trigger[32];
    char code_pattern[32];
    char code_label[64];
    char insert_list[24];
    char scroll_symbol[32];
    char sync_group[32];
    char frame_driver[24];
    char group_role[24];
    char parent_actor[64];
    char frames[MAX_RUNTIME_ACTOR_FRAMES][64];
    int frame_dx[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_dy[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_ticks_override[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_hfl_mode[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_vfl_mode[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_count;
    int source_object;
    int x;
    int y;
    int layer;
    float scroll;
    int frame_ticks;
    int phase_ticks;
    int delay_min_ticks;
    int delay_max_ticks;
    int motion_x;
    int motion_y;
    int group_index;
    int part_index;
    int part_count;
    int hfl;
    int vfl;
    bool enabled;
    bool loop;
    bool replace_source;
    bool emit_bgnd_code;
    bool sync_group_timing;
    bool multipart_piece;
    bool screen_space_y;
};
static const char *const k_runtime_actor_group_roles[] = {
    "solo",
    "leader",
    "follower",
    "multipart_piece"
};

static const char *const k_runtime_actor_flip_modes[] = {
    "inherit",
    "off",
    "on"
};


static RuntimeStageActor g_runtime_actors[MAX_RUNTIME_ACTORS];
static int  g_runtime_actor_count = 0;
static int  g_runtime_actor_selected = -1;
static bool g_runtime_actor_preview = true;
static bool g_runtime_actor_labels = true;
static bool g_runtime_actor_timeline_paused = false;
static bool g_runtime_actor_onion_skin = false;
static bool g_runtime_actor_isolation_open = false;
static int  g_runtime_actor_scrub_frame = 0;
static float g_runtime_actor_playback_speed = 1.0f;
static float g_runtime_actor_isolation_zoom = 3.0f;
static int  g_runtime_actor_group_nudge_x = 0;
static int  g_runtime_actor_group_nudge_y = 0;
static bool g_runtime_actor_frame_clip_valid = false;
static int  g_runtime_actor_frame_clip_dx = 0;
static int  g_runtime_actor_frame_clip_dy = 0;
static int  g_runtime_actor_frame_clip_ticks = 0;
static int  g_runtime_actor_frame_clip_hfl = 0;
static int  g_runtime_actor_frame_clip_vfl = 0;
static char g_runtime_actor_status[512] = "";
static int  g_runtime_actor_preview_base_images = -1;
static int  g_runtime_actor_preview_base_pals = -1;
static int runtime_actor_frame_ticks_at(const RuntimeStageActor *actor, int frame);
static void runtime_actor_init_default(RuntimeStageActor *actor);
static void runtime_actor_select(int idx);
bool runtime_actor_preview_imports_loaded(void);

static const char *const k_runtime_actor_preview_source_prefix = "RTPREVIEW:";

struct InferredRuntimeFrameGroup {
    char base[64];
    char source[64];
    int frames[MAX_RUNTIME_ACTOR_FRAMES];
    int order[MAX_RUNTIME_ACTOR_FRAMES];
    int frame_count;
    bool strong_metadata;
};

static void runtime_actor_status(const char *msg)
{
    snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status, "%s", msg ? msg : "");
    if (msg && msg[0]) stage_set_toast(msg);
}

static const char *const k_runtime_actor_patterns[] = {
    "loop_sprite",
    "synced_group",
    "moving_recycle",
    "delayed_random",
    "module_cycle",
    "scripted_custom"
};

static const char *const k_runtime_actor_frame_drivers[] = {
    "frame_a9",
    "framew",
    "mframew",
    "init_anirate",
    "custom"
};

static bool runtime_actor_code_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void runtime_actor_make_code_label(const char *src, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!src || !src[0]) src = "runtime_actor";
    size_t n = 0;
    char first = src[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
        snprintf(out, outsz, "rt_");
        n = strlen(out);
    }
    for (const char *p = src; *p && n + 1 < outsz; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[n++] = runtime_actor_code_char(c) ? c : '_';
    }
    out[n] = '\0';
}

static int runtime_actor_string_index(const char *value, const char *const *items, int count)
{
    if (!value) value = "";
    for (int i = 0; i < count; i++) {
        if (strcmp(value, items[i]) == 0) return i;
    }
    return -1;
}

static void runtime_actor_combo_string(const char *label, char *value, size_t valuesz,
                                       const char *const *items, int count)
{
    if (!value || valuesz == 0 || !items || count <= 0) return;
    int idx = runtime_actor_string_index(value, items, count);
    const char *preview = idx >= 0 ? items[idx] : (value[0] ? value : items[0]);
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; i++) {
            bool selected = (i == idx);
            if (ImGui::Selectable(items[i], selected))
                snprintf(value, valuesz, "%s", items[i]);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

static void runtime_actor_appendf(char *buf, size_t bufsz, const char *fmt, ...)
{
    if (!buf || bufsz == 0) return;
    size_t n = strlen(buf);
    if (n >= bufsz - 1) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, bufsz - n, fmt, ap);
    va_end(ap);
}

static void runtime_actor_copy_bgnd_notes(const RuntimeStageActor *a)
{
    if (!a) return;
    char label[96];
    runtime_actor_make_code_label(a->code_label[0] ? a->code_label : a->name, label, sizeof label);
    const char *insert_list = a->insert_list[0] ? a->insert_list : "baklst4";
    const char *scroll_symbol = a->scroll_symbol[0] ? a->scroll_symbol : "worldtlx4";
    const char *driver = a->frame_driver[0] ? a->frame_driver : "frame_a9";
    const char *pattern = a->code_pattern[0] ? a->code_pattern : "loop_sprite";
    uint32_t packed_xy = (((uint32_t)a->y & 0xffffu) << 16) | ((uint32_t)a->x & 0xffffu);

    char out[8192];
    out[0] = '\0';
    runtime_actor_appendf(out, sizeof out, "; Runtime actor: %s\n", a->name);
    runtime_actor_appendf(out, sizeof out, "; Pattern: %s, driver: %s, trigger: %s\n", pattern, driver, a->trigger);
    runtime_actor_appendf(out, sizeof out, "; Insert: %s, display scroll: %s, preview scroll %.4f\n", insert_list, scroll_symbol, a->scroll);
    if (a->screen_space_y)
        runtime_actor_appendf(out, sizeof out, "; Y is screen-space, matching BGND set_xy_coordinates display-object placement\n");
    if (a->sync_group[0]) runtime_actor_appendf(out, sizeof out, "; Sync group: %s\n", a->sync_group);
    if (a->sync_group_timing) runtime_actor_appendf(out, sizeof out, "; Group timing is phase-locked to group leader\n");
    if (a->multipart_piece)
        runtime_actor_appendf(out, sizeof out, "; Multipart piece: %d/%d role=%s parent=%s\n",
                              a->part_index + 1, a->part_count, a->group_role, a->parent_actor);
    if (a->delay_min_ticks || a->delay_max_ticks)
        runtime_actor_appendf(out, sizeof out, "; Delay window: %d..%d ticks\n", a->delay_min_ticks, a->delay_max_ticks);
    if (a->motion_x || a->motion_y)
        runtime_actor_appendf(out, sizeof out, "; Motion hint: oxvel=%d oyvel=%d\n", a->motion_x, a->motion_y);
    runtime_actor_appendf(out, sizeof out, "; call from the stage calla with: create pid_bani,%s_proc\n\n", label);
    runtime_actor_appendf(out, sizeof out, "%s_proc\n", label);
    runtime_actor_appendf(out, sizeof out, "\tmovi\t%s,a5\n", a->frame_count > 0 ? a->frames[0] : "FRAME1");
    runtime_actor_appendf(out, sizeof out, "\tcalla\tgso_dmawnz\n");
    runtime_actor_appendf(out, sizeof out, "\tmovi\t0%08xh,a4\t\t; y=%d x=%d from bddtool\n", packed_xy, a->y, a->x);
    runtime_actor_appendf(out, sizeof out, "\tcalla\tset_xy_coordinates\n");
    if (a->hfl) runtime_actor_appendf(out, sizeof out, "\tcalla\tflip_single\n");
    runtime_actor_appendf(out, sizeof out, "\tmove\ta8,a0\n");
    runtime_actor_appendf(out, sizeof out, "\tmovi\t%s,b4\n", insert_list);
    runtime_actor_appendf(out, sizeof out, "\tcalla\tinsobj_v\n\n");
    runtime_actor_appendf(out, sizeof out, "%s_loop\n", label);
    runtime_actor_appendf(out, sizeof out, "\tmovi\t%s_ani,a9\n", label);
    runtime_actor_appendf(out, sizeof out, "\tmovk\t%d,a0\t\t; frame ticks\n", a->frame_ticks > 0 ? a->frame_ticks : 1);
    runtime_actor_appendf(out, sizeof out, "\t; %s: verify final driver against BGND.ASM pattern before commit\n", driver);
    runtime_actor_appendf(out, sizeof out, "\t; frame_a9 + sleep matches Pit II; mframew is for multipart actors\n");
    runtime_actor_appendf(out, sizeof out, "\tjruc\t%s_loop\n\n", label);
    runtime_actor_appendf(out, sizeof out, "%s_ani\n", label);
    for (int i = 0; i < a->frame_count; i++)
        runtime_actor_appendf(out, sizeof out, "\t.long\t%s\t\t; dx=%d dy=%d ticks=%d hfl=%d vfl=%d\n",
                              a->frames[i][0] ? a->frames[i] : "0", a->frame_dx[i], a->frame_dy[i],
                              runtime_actor_frame_ticks_at(a, i), a->frame_hfl_mode[i], a->frame_vfl_mode[i]);
    if (a->loop)
        runtime_actor_appendf(out, sizeof out, "\t.long\tani_jump,%s_ani\n", label);
    else
        runtime_actor_appendf(out, sizeof out, "\t.long\t0\n");
    ImGui::SetClipboardText(out);
    stage_set_toast("Copied BGND actor skeleton");
}

static const char *runtime_actor_image_label(const Img *im, char *out, size_t outsz)
{
    if (!out || outsz == 0) return "";
    if (!im) {
        snprintf(out, outsz, "");
    } else if (im->label[0]) {
        snprintf(out, outsz, "%s", im->label);
    } else if (im->source[0]) {
        snprintf(out, outsz, "%s", im->source);
    } else {
        snprintf(out, outsz, "img_%04X", im->idx);
    }
    return out;
}

static int runtime_actor_active_image_index(void)
{
    int obj_i = active_object_index();
    if (obj_i >= 0 && obj_i < g_no) {
        Img *im = img_find(g_obj[obj_i].ii);
        if (im) return (int)(im - g_img);
    }
    return active_image_index();
}

static bool runtime_actor_active_image_label(char *out, size_t outsz)
{
    int img_i = runtime_actor_active_image_index();
    if (img_i < 0 || img_i >= g_ni) return false;
    runtime_actor_image_label(&g_img[img_i], out, outsz);
    return out && out[0];
}

static int runtime_actor_frame_image_index(const RuntimeStageActor *actor, int frame)
{
    if (!actor || actor->frame_count <= 0) return -1;
    if (frame < 0) frame = 0;
    if (frame >= actor->frame_count) frame = actor->frame_count - 1;
    const char *label = actor->frames[frame];
    if (!label || !label[0]) return -1;
    int idx = find_img_by_label_casefold(label);
    if (idx >= 0) return idx;
    if ((label[0] == '0' && (label[1] == 'x' || label[1] == 'X')) ||
        (label[0] >= '0' && label[0] <= '9')) {
        char *end = NULL;
        long image_idx = strtol(label, &end, 0);
        if (end && *end == '\0') {
            Img *im = img_find((int)image_idx);
            if (im) return (int)(im - g_img);
        }
    }
    return -1;
}

static int runtime_actor_frame_ticks_at(const RuntimeStageActor *actor, int frame)
{
    if (!actor || frame < 0 || frame >= MAX_RUNTIME_ACTOR_FRAMES) return 1;
    if (actor->frame_ticks_override[frame] > 0) return actor->frame_ticks_override[frame];
    return actor->frame_ticks > 0 ? actor->frame_ticks : 1;
}

static bool runtime_actor_image_has_anim_metadata(const Img *im)
{
    if (!im) return false;
    return im->frm || im->opals || im->pttblnum ||
           im->anix || im->aniy || im->anix2 || im->aniy2 || im->aniz2;
}

static bool runtime_actor_image_has_strong_anim_metadata(const Img *im)
{
    if (!im) return false;
    return im->frm || im->opals || im->pttblnum;
}

static bool runtime_actor_label_has_anim_hint(const char *s)
{
    if (!s || !s[0]) return false;
    char upper[64];
    size_t n = 0;
    for (; s[n] && n + 1 < sizeof upper; n++) {
        char c = s[n];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        upper[n] = c;
    }
    upper[n] = '\0';
    return strstr(upper, "FACE") || strstr(upper, "ANIM") ||
           strstr(upper, "FLAME") || strstr(upper, "FIRE") ||
           strstr(upper, "WATER") || strstr(upper, "SMOKE") ||
           strstr(upper, "CLOUD") || strstr(upper, "SPIN");
}

static bool runtime_actor_parse_numbered_frame_key(const Img *im,
                                                   char *base, size_t basesz,
                                                   int *order)
{
    if (!im || !base || basesz == 0 || !order) return false;
    const char *label = im->label[0] ? im->label : "";
    if (!label[0]) return false;

    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s", label);
    int len = (int)strlen(tmp);
    while (len > 0 && (tmp[len - 1] == ' ' || tmp[len - 1] == '\t'))
        tmp[--len] = '\0';
    int digit_start = len;
    while (digit_start > 0 && tmp[digit_start - 1] >= '0' && tmp[digit_start - 1] <= '9')
        digit_start--;
    if (digit_start == len || digit_start <= 1)
        return false;

    *order = atoi(tmp + digit_start);
    int base_len = digit_start;
    while (base_len > 0 &&
           (tmp[base_len - 1] == '_' || tmp[base_len - 1] == '-' ||
            tmp[base_len - 1] == '.' || tmp[base_len - 1] == ' '))
        base_len--;
    if (base_len <= 1) return false;
    if ((size_t)base_len >= basesz) base_len = (int)basesz - 1;
    memcpy(base, tmp, (size_t)base_len);
    base[base_len] = '\0';
    return true;
}

static int runtime_actor_find_inferred_group(InferredRuntimeFrameGroup *groups,
                                             int group_count,
                                             const char *base,
                                             const char *source)
{
    if (!groups || !base) return -1;
    if (!source) source = "";
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].base, base) == 0 &&
            strcmp(groups[i].source, source) == 0)
            return i;
    }
    return -1;
}

static void runtime_actor_sort_inferred_group(InferredRuntimeFrameGroup *g)
{
    if (!g) return;
    for (int i = 1; i < g->frame_count; i++) {
        int frame = g->frames[i];
        int order = g->order[i];
        int j = i - 1;
        while (j >= 0 && g->order[j] > order) {
            g->frames[j + 1] = g->frames[j];
            g->order[j + 1] = g->order[j];
            j--;
        }
        g->frames[j + 1] = frame;
        g->order[j + 1] = order;
    }
}

static int runtime_actor_collect_inferred_groups(InferredRuntimeFrameGroup *groups,
                                                 int max_groups)
{
    if (!groups || max_groups <= 0) return 0;
    int group_count = 0;
    for (int i = 0; i < g_ni; i++) {
        Img *im = &g_img[i];
        if (!runtime_actor_image_has_anim_metadata(im)) continue;

        char base[64];
        int order = 0;
        if (!runtime_actor_parse_numbered_frame_key(im, base, sizeof base, &order))
            continue;

        const char *source = im->source[0] ? im->source : "";
        int gi = runtime_actor_find_inferred_group(groups, group_count, base, source);
        if (gi < 0) {
            if (group_count >= max_groups) continue;
            gi = group_count++;
            snprintf(groups[gi].base, sizeof groups[gi].base, "%s", base);
            snprintf(groups[gi].source, sizeof groups[gi].source, "%s", source);
            groups[gi].frame_count = 0;
            groups[gi].strong_metadata = false;
        }
        InferredRuntimeFrameGroup *g = &groups[gi];
        if (g->frame_count >= MAX_RUNTIME_ACTOR_FRAMES)
            continue;
        bool dup = false;
        for (int fi = 0; fi < g->frame_count; fi++) {
            if (g->frames[fi] == i) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        g->frames[g->frame_count] = i;
        g->order[g->frame_count] = order;
        g->frame_count++;
        if (runtime_actor_image_has_strong_anim_metadata(im) ||
            runtime_actor_label_has_anim_hint(base))
            g->strong_metadata = true;
    }

    int out_count = 0;
    for (int gi = 0; gi < group_count; gi++) {
        InferredRuntimeFrameGroup *g = &groups[gi];
        runtime_actor_sort_inferred_group(g);
        if (g->frame_count < 2 || !g->strong_metadata)
            continue;
        if (out_count != gi)
            groups[out_count] = groups[gi];
        out_count++;
    }
    return out_count;
}

static int runtime_actor_group_contains_image(const InferredRuntimeFrameGroup *g, int img_i)
{
    if (!g) return -1;
    for (int fi = 0; fi < g->frame_count; fi++)
        if (g->frames[fi] == img_i) return fi;
    return -1;
}

static bool runtime_actor_has_source_object_actor(int obj_i)
{
    if (obj_i < 0) return false;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        if (a->source_object == obj_i) return true;
    }
    return false;
}

static void runtime_actor_init_default(RuntimeStageActor *actor)
{
    if (!actor) return;
    memset(actor, 0, sizeof *actor);
    snprintf(actor->trigger, sizeof actor->trigger, "always");
    snprintf(actor->code_pattern, sizeof actor->code_pattern, "loop_sprite");
    snprintf(actor->insert_list, sizeof actor->insert_list, "baklst4");
    snprintf(actor->scroll_symbol, sizeof actor->scroll_symbol, "worldtlx4");
    snprintf(actor->frame_driver, sizeof actor->frame_driver, "frame_a9");
    snprintf(actor->group_role, sizeof actor->group_role, "solo");
    actor->source_object = -1;
    actor->frame_ticks = 6;
    actor->loop = true;
    actor->enabled = true;
    actor->scroll = 1.0f;
    actor->emit_bgnd_code = false;
    actor->part_count = 1;
    actor->screen_space_y = true;
}

static const RuntimeStageActor *runtime_actor_group_leader(const RuntimeStageActor *actor)
{
    if (!actor || !actor->sync_group_timing || !actor->sync_group[0]) return actor;
    const RuntimeStageActor *best = actor;
    int best_score = 0x7fffffff;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *cand = &g_runtime_actors[i];
        if (!cand->enabled || strcmp(cand->sync_group, actor->sync_group) != 0) continue;
        int score = cand->group_index;
        if (strcmp(cand->group_role, "leader") == 0) score -= 1000000;
        if (score < best_score) {
            best = cand;
            best_score = score;
        }
    }
    return best;
}

static int runtime_actor_timeline_ticks(const RuntimeStageActor *actor)
{
    const RuntimeStageActor *basis = runtime_actor_group_leader(actor);
    int phase = basis ? basis->phase_ticks : (actor ? actor->phase_ticks : 0);
    int t = (int)(ImGui::GetTime() * 53.0 * (double)g_runtime_actor_playback_speed) + phase;
    return t < 0 ? 0 : t;
}

static bool runtime_actor_frame_hfl_at(const RuntimeStageActor *actor, int frame)
{
    bool hfl = actor && actor->hfl != 0;
    if (!actor || frame < 0 || frame >= MAX_RUNTIME_ACTOR_FRAMES) return hfl;
    if (actor->frame_hfl_mode[frame] == 1) return false;
    if (actor->frame_hfl_mode[frame] == 2) return true;
    return hfl;
}

static bool runtime_actor_frame_vfl_at(const RuntimeStageActor *actor, int frame)
{
    bool vfl = actor && actor->vfl != 0;
    if (!actor || frame < 0 || frame >= MAX_RUNTIME_ACTOR_FRAMES) return vfl;
    if (actor->frame_vfl_mode[frame] == 1) return false;
    if (actor->frame_vfl_mode[frame] == 2) return true;
    return vfl;
}

static int runtime_actor_current_frame(const RuntimeStageActor *actor)
{
    if (!actor || actor->frame_count <= 1) return 0;
    if (g_runtime_actor_timeline_paused) return std::min(std::max(g_runtime_actor_scrub_frame, 0), actor->frame_count - 1);
    int total = 0;
    for (int i = 0; i < actor->frame_count; i++)
        total += runtime_actor_frame_ticks_at(actor, i);
    if (total <= 0) return 0;

    int t = runtime_actor_timeline_ticks(actor);
    if (actor->loop) {
        t %= total;
    } else if (t >= total) {
        return actor->frame_count - 1;
    }

    int cursor = 0;
    for (int i = 0; i < actor->frame_count; i++) {
        int ticks = runtime_actor_frame_ticks_at(actor, i);
        if (t < cursor + ticks) return i;
        cursor += ticks;
    }
    return actor->frame_count - 1;
}

static void runtime_actor_default_sidecar_path(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    const char *base = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (base && base[0]) {
        snprintf(out, outsz, "%s", base);
        char *dot = strrchr(out, '.');
        if (dot) *dot = '\0';
        size_t n = strlen(out);
        snprintf(out + n, outsz - n, ".runtime.json");
        return;
    }
    if (g_stage_dir[0]) {
        path_join(out, outsz, g_stage_dir, "stage_runtime.json");
        return;
    }
    snprintf(out, outsz, "stage_runtime.json");
}

bool runtime_actor_image_is_preview_import(const Img *im)
{
    size_t prefix_len = strlen(k_runtime_actor_preview_source_prefix);
    return im && strncmp(im->source, k_runtime_actor_preview_source_prefix, prefix_len) == 0;
}

void runtime_actor_mark_preview_import_range(int image_base, int palette_base,
                                             int start, int end,
                                             const char *source_label)
{
    if (start < 0) start = 0;
    if (end > g_ni) end = g_ni;
    if (end <= start) return;

    if (!runtime_actor_preview_imports_loaded()) {
        g_runtime_actor_preview_base_images = image_base >= 0 ? image_base : start;
        g_runtime_actor_preview_base_pals = palette_base >= 0 ? palette_base : g_n_pals;
    }

    for (int i = start; i < end && i < g_ni; i++) {
        Img *im = &g_img[i];
        if (runtime_actor_image_is_preview_import(im))
            continue;

        char label[64];
        if (source_label && source_label[0]) {
            snprintf(label, sizeof label, "%s", path_basename_ptr(source_label));
        } else if (im->source[0]) {
            snprintf(label, sizeof label, "%s", im->source);
        } else {
            snprintf(label, sizeof label, "runtime");
        }
        snprintf(im->source, sizeof im->source, "%s%.52s",
                 k_runtime_actor_preview_source_prefix, label);
    }
}

int runtime_actor_preview_import_count(void)
{
    int count = 0;
    for (int i = 0; i < g_ni; i++)
        if (runtime_actor_image_is_preview_import(&g_img[i])) count++;
    return count;
}

bool runtime_actor_preview_imports_loaded(void)
{
    return runtime_actor_preview_import_count() > 0;
}

static int runtime_actor_first_preview_import(void)
{
    for (int i = 0; i < g_ni; i++)
        if (runtime_actor_image_is_preview_import(&g_img[i])) return i;
    return -1;
}

static bool runtime_actor_preview_imports_are_tail(void)
{
    int first = runtime_actor_first_preview_import();
    if (first < 0) return true;
    for (int i = first; i < g_ni; i++)
        if (!runtime_actor_image_is_preview_import(&g_img[i])) return false;
    return true;
}

void runtime_actor_preview_import_status(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    int count = runtime_actor_preview_import_count();
    if (count <= 0) {
        snprintf(out, outsz, "No runtime preview sprites are loaded.");
        return;
    }
    snprintf(out, outsz, "%d runtime preview sprite(s) are loaded%s.",
             count, runtime_actor_preview_imports_are_tail() ? "" : " with later non-preview imports");
}

static bool runtime_actor_stage_dir(char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    const char *base = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (!base || !base[0]) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, outsz, "%s", base);
    char *slash = strrchr(out, '/');
    char *backslash = strrchr(out, '\\');
    char *sep = slash;
    if (!sep || (backslash && backslash > sep)) sep = backslash;
    if (!sep) {
        snprintf(out, outsz, ".");
        return true;
    }
    *sep = '\0';
    return out[0] != '\0';
}

static bool runtime_actor_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nlen && h[i]) {
            char a = h[i];
            char b = needle[i];
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

int runtime_actor_count(void)
{
    return g_runtime_actor_count;
}

int runtime_actor_total_frame_count(void)
{
    int total = 0;
    for (int i = 0; i < g_runtime_actor_count; i++)
        total += g_runtime_actors[i].frame_count;
    return total;
}

int runtime_actor_missing_frame_count(void)
{
    int missing = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *actor = &g_runtime_actors[i];
        for (int frame = 0; frame < actor->frame_count; frame++) {
            if (runtime_actor_frame_image_index(actor, frame) < 0)
                missing++;
        }
    }
    return missing;
}

int runtime_actor_name_contains_count(const char *needle)
{
    int matches = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        if (runtime_actor_contains_ci(g_runtime_actors[i].name, needle))
            matches++;
    }
    return matches;
}

bool runtime_actor_info(int actor_index, char *name, size_t namesz,
                        int *x, int *y, int *screen_space_y)
{
    if (actor_index < 0 || actor_index >= g_runtime_actor_count)
        return false;
    const RuntimeStageActor *actor = &g_runtime_actors[actor_index];
    if (name && namesz > 0)
        snprintf(name, namesz, "%s", actor->name);
    if (x) *x = actor->x;
    if (y) *y = actor->y;
    if (screen_space_y) *screen_space_y = actor->screen_space_y ? 1 : 0;
    return true;
}

static bool runtime_actor_stage_is_forest(void)
{
    if (g_name[0]) return runtime_actor_contains_ci(g_name, "FOREST");
    if (g_bdb_path[0] || g_bdd_path[0])
        return runtime_actor_contains_ci(g_bdb_path, "FOREST") ||
               runtime_actor_contains_ci(g_bdd_path, "FOREST");
    return runtime_actor_contains_ci(g_stage_internal_name, "FOREST");
}

static int runtime_actor_import_stage_runtime_art(void)
{
    int imported = 0;
    int old_dirty = g_dirty;
    int old_need_rebuild = g_need_rebuild;
    bool old_palette_dirty = g_mk2_palette_sync_dirty;

    if (runtime_actor_stage_is_forest()) {
        const char *forest_labels[] = {
            "TREEANI1", "TREEANI2", "TREEANI3", "TREEANI4",
            "TREEANI5", "TREEANI6", "TREEANI7",
            "NJSMOKEHIDE", "JADEHIDDEN"
        };
        imported += import_runtime_lod_source_labels("MK6MIL.LOD",
                                                     forest_labels,
                                                     (int)(sizeof forest_labels / sizeof forest_labels[0]),
                                                     false);
    }

    g_dirty = old_dirty;
    g_mk2_palette_sync_dirty = old_palette_dirty;
    if (imported > 0)
        g_need_rebuild = 1;
    else
        g_need_rebuild = old_need_rebuild;
    return imported;
}

static bool runtime_actor_all_labels_available(const char *const *labels, int count)
{
    for (int i = 0; i < count; i++)
        if (find_img_by_label_casefold(labels[i]) < 0)
            return false;
    return true;
}

static int runtime_actor_add_forest_tree_actors(void)
{
    if (!runtime_actor_stage_is_forest())
        return 0;

    const char *seq[] = {
        "TREEANI1", "TREEANI2", "TREEANI3", "TREEANI4", "TREEANI5", "TREEANI6", "TREEANI7",
        "TREEANI6", "TREEANI7", "TREEANI6", "TREEANI7", "TREEANI6", "TREEANI7", "TREEANI6", "TREEANI7",
        "TREEANI5", "TREEANI4", "TREEANI3", "TREEANI2", "TREEANI1"
    };
    const char *required[] = {
        "TREEANI1", "TREEANI2", "TREEANI3", "TREEANI4",
        "TREEANI5", "TREEANI6", "TREEANI7"
    };
    if (!runtime_actor_all_labels_available(required, (int)(sizeof required / sizeof required[0])))
        return 0;

    int first_img_i = find_img_by_label_casefold("TREEANI1");
    Img *first_im = (first_img_i >= 0 && first_img_i < g_ni) ? &g_img[first_img_i] : NULL;
    int ax = first_im ? img_anim_offset_x(first_im, 0) : 0;
    int ay = first_im ? img_anim_offset_y(first_im, 0) : 0;

    const int packed_x[] = { 0x0148, 0x0270, 0x0391 };
    const int packed_y = 0x0049;
    int added = 0;
    for (int i = 0; i < 3 && g_runtime_actor_count < MAX_RUNTIME_ACTORS; i++) {
        RuntimeStageActor actor;
        runtime_actor_init_default(&actor);
        snprintf(actor.name, sizeof actor.name, "forest_tree_face_%d", i + 1);
        runtime_actor_make_code_label(actor.name, actor.code_label, sizeof actor.code_label);
        snprintf(actor.trigger, sizeof actor.trigger, "stage");
        snprintf(actor.insert_list, sizeof actor.insert_list, "baklst1");
        snprintf(actor.scroll_symbol, sizeof actor.scroll_symbol, "worldtlx");
        snprintf(actor.frame_driver, sizeof actor.frame_driver, "triple_framew");
        actor.x = packed_x[i] - ax;
        actor.y = packed_y - ay;
        actor.layer = 0x40;
        actor.scroll = 1.0f;
        actor.frame_ticks = 5;
        actor.frame_count = (int)(sizeof seq / sizeof seq[0]);
        for (int fi = 0; fi < actor.frame_count; fi++) {
            int frame_img_i;
            Img *frame_im;
            snprintf(actor.frames[fi], sizeof actor.frames[fi], "%s", seq[fi]);
            frame_img_i = find_img_by_label_casefold(seq[fi]);
            frame_im = (frame_img_i >= 0 && frame_img_i < g_ni) ? &g_img[frame_img_i] : NULL;
            actor.frame_dx[fi] = ax - (frame_im ? img_anim_offset_x(frame_im, actor.hfl) : ax);
            actor.frame_dy[fi] = ay - (frame_im ? img_anim_offset_y(frame_im, actor.vfl) : ay);
        }
        g_runtime_actors[g_runtime_actor_count++] = actor;
        added++;
    }

    if (added > 0) {
        runtime_actor_select(0);
        g_runtime_actor_preview = true;
        snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status,
                 "Loaded Forest tree face runtime animation (%d actors)", added);
    }
    return added;
}

static void runtime_actor_import_deadpool_imgs(void)
{
    char dir[512];
    if (!runtime_actor_stage_dir(dir, sizeof dir)) {
        runtime_actor_status("Open NUPOOL.BDB before importing runtime sprites");
        return;
    }

    struct Source {
        const char *file;
        const char *probe_label;
    };
    const Source sources[] = {
        { "MKHANG.IMG", "EDHANG1A" },
        { "ACIDHANG.IMG", "DEDHANG1A" },
    };

    int imported = 0;
    int skipped = 0;
    int missing = 0;
    int old_dirty = g_dirty;
    int old_need_rebuild = g_need_rebuild;
    bool old_palette_dirty = g_mk2_palette_sync_dirty;
    if (!runtime_actor_preview_imports_loaded()) {
        g_runtime_actor_preview_base_images = g_ni;
        g_runtime_actor_preview_base_pals = g_n_pals;
    }
    for (int i = 0; i < (int)(sizeof sources / sizeof sources[0]); i++) {
        if (find_img_by_label_casefold(sources[i].probe_label) >= 0) {
            skipped++;
            continue;
        }
        char path[640];
        path_join(path, sizeof path, dir, sources[i].file);
        if (!stage_file_exists(path)) {
            missing++;
            continue;
        }
        int start_img = g_ni;
        int n = import_img_file(path, false);
        if (n > 0) {
            imported += n;
            for (int j = start_img; j < g_ni; j++)
                snprintf(g_img[j].source, sizeof g_img[j].source, "%s%s",
                         k_runtime_actor_preview_source_prefix, sources[i].file);
        }
    }
    if (imported <= 0 && !runtime_actor_preview_imports_loaded()) {
        g_runtime_actor_preview_base_images = -1;
        g_runtime_actor_preview_base_pals = -1;
    }

    g_dirty = old_dirty;
    g_mk2_palette_sync_dirty = old_palette_dirty;
    g_need_rebuild = imported > 0 ? 1 : old_need_rebuild;

    char msg[192];
    snprintf(msg, sizeof msg, "Runtime IMG import: %d sprite(s), %d already loaded, %d missing source(s)",
             imported, skipped, missing);
    runtime_actor_status(msg);
}

static void runtime_actor_discard_preview_imports(void)
{
    int count = runtime_actor_preview_import_count();
    if (count <= 0) {
        runtime_actor_status("No runtime preview sprites are loaded");
        return;
    }
    if (!runtime_actor_preview_imports_are_tail() ||
        g_runtime_actor_preview_base_images < 0 ||
        g_runtime_actor_preview_base_pals < 0) {
        runtime_actor_status("Cannot safely discard preview imports after other image changes; close without saving and reopen NUPOOL.BDB");
        return;
    }

    int old_dirty = g_dirty;
    bool old_palette_dirty = g_mk2_palette_sync_dirty;
    editor_project_truncate_images(g_runtime_actor_preview_base_images);
    editor_project_truncate_palettes(g_runtime_actor_preview_base_pals);
    g_runtime_actor_preview_base_images = -1;
    g_runtime_actor_preview_base_pals = -1;
    g_dirty = old_dirty;
    g_mk2_palette_sync_dirty = old_palette_dirty;
    g_need_rebuild = 1;
    g_last_import_img = g_ni > 0 ? g_ni - 1 : -1;
    runtime_actor_status("Discarded runtime preview IMG imports");
}

static RuntimeStageActor *runtime_actor_selected_ptr(void)
{
    if (g_runtime_actor_selected < 0 || g_runtime_actor_selected >= g_runtime_actor_count)
        return NULL;
    return &g_runtime_actors[g_runtime_actor_selected];
}

static void runtime_actor_select(int idx)
{
    if (idx < 0 || idx >= g_runtime_actor_count) idx = g_runtime_actor_count > 0 ? 0 : -1;
    g_runtime_actor_selected = idx;
}

static void runtime_actor_remove_selected(void)
{
    int idx = g_runtime_actor_selected;
    if (idx < 0 || idx >= g_runtime_actor_count) return;
    for (int i = idx; i + 1 < g_runtime_actor_count; i++)
        g_runtime_actors[i] = g_runtime_actors[i + 1];
    g_runtime_actor_count--;
    runtime_actor_select(std::min(idx, g_runtime_actor_count - 1));
}

static int runtime_actor_add_from_selected_object(void)
{
    int obj_i = active_object_index();
    if (obj_i < 0 || obj_i >= g_no) {
        runtime_actor_status("Select a BDB object first");
        return -1;
    }
    if (g_runtime_actor_count >= MAX_RUNTIME_ACTORS) {
        runtime_actor_status("Runtime actor list is full");
        return -1;
    }

    Obj *obj = &g_obj[obj_i];
    Img *im = img_find(obj->ii);
    if (!im) {
        runtime_actor_status("Selected object has no image");
        return -1;
    }

    RuntimeStageActor actor = {};
    char label[64];
    runtime_actor_image_label(im, label, sizeof label);
    snprintf(actor.name, sizeof actor.name, "%s_actor", label[0] ? label : "runtime");
    snprintf(actor.trigger, sizeof actor.trigger, "always");
    snprintf(actor.code_pattern, sizeof actor.code_pattern, "loop_sprite");
    runtime_actor_make_code_label(actor.name, actor.code_label, sizeof actor.code_label);
    snprintf(actor.insert_list, sizeof actor.insert_list, "baklst4");
    snprintf(actor.scroll_symbol, sizeof actor.scroll_symbol, "worldtlx4");
    snprintf(actor.sync_group, sizeof actor.sync_group, "");
    snprintf(actor.frame_driver, sizeof actor.frame_driver, "frame_a9");
    snprintf(actor.group_role, sizeof actor.group_role, "solo");
    snprintf(actor.parent_actor, sizeof actor.parent_actor, "");
    snprintf(actor.frames[0], sizeof actor.frames[0], "%s", label);
    actor.frame_count = 1;
    actor.source_object = obj_i;
    actor.x = obj->depth;
    actor.y = obj->sy;
    actor.layer = (obj->wx >> 8) & 0xFF;
    actor.scroll = mk2_scroll_factor_for_layer(actor.layer);
    actor.frame_ticks = 6;
    actor.phase_ticks = 0;
    actor.group_index = 0;
    actor.part_index = 0;
    actor.part_count = 1;
    actor.hfl = obj->hfl ? 1 : 0;
    actor.vfl = obj->vfl ? 1 : 0;
    actor.enabled = true;
    actor.loop = true;
    actor.replace_source = true;
    actor.emit_bgnd_code = true;
    actor.sync_group_timing = false;
    actor.multipart_piece = false;
    actor.screen_space_y = false;

    int dst = g_runtime_actor_count++;
    g_runtime_actors[dst] = actor;
    runtime_actor_select(dst);
    g_runtime_actor_preview = true;
    runtime_actor_status("Created runtime actor from selected object");
    return dst;
}

static void runtime_actor_append_active_frame(RuntimeStageActor *actor)
{
    if (!actor) return;

    if (actor->frame_count >= MAX_RUNTIME_ACTOR_FRAMES) {
        runtime_actor_status("Frame list is full");
        return;
    }
    char label[64];
    if (!runtime_actor_active_image_label(label, sizeof label)) {
        runtime_actor_status("Select an object or image to append as a frame");
        return;
    }
    snprintf(actor->frames[actor->frame_count], sizeof actor->frames[0], "%s", label);
    actor->frame_dx[actor->frame_count] = 0;
    actor->frame_dy[actor->frame_count] = 0;
    actor->frame_ticks_override[actor->frame_count] = 0;
    actor->frame_hfl_mode[actor->frame_count] = 0;
    actor->frame_vfl_mode[actor->frame_count] = 0;
    actor->frame_count++;
}

static void runtime_actor_replace_anchor_from_selected(RuntimeStageActor *actor)
{
    if (!actor) return;
    int obj_i = active_object_index();
    if (obj_i < 0 || obj_i >= g_no) {
        runtime_actor_status("Select a BDB object first");
        return;
    }
    Obj *obj = &g_obj[obj_i];
    actor->source_object = obj_i;
    actor->x = obj->depth;
    actor->y = obj->sy;
    actor->layer = (obj->wx >> 8) & 0xFF;
    actor->scroll = mk2_scroll_factor_for_layer(actor->layer);
    actor->hfl = obj->hfl ? 1 : 0;
    actor->vfl = obj->vfl ? 1 : 0;
    actor->screen_space_y = false;
    runtime_actor_status("Runtime actor anchor updated from selected object");
}

static void runtime_actor_set_current_frame_from_selected(RuntimeStageActor *actor)
{
    if (!actor || actor->frame_count <= 0) return;
    int frame = runtime_actor_current_frame(actor);
    if (frame < 0 || frame >= actor->frame_count) frame = 0;
    int obj_i = active_object_index();
    if (obj_i < 0 || obj_i >= g_no) {
        runtime_actor_status("Select a BDB object first");
        return;
    }
    Obj *obj = &g_obj[obj_i];
    Img *im = img_find(obj->ii);
    if (!im) {
        runtime_actor_status("Selected object has no image");
        return;
    }
    char label[64];
    runtime_actor_image_label(im, label, sizeof label);
    snprintf(actor->frames[frame], sizeof actor->frames[frame], "%s", label);
    actor->frame_dx[frame] = obj->depth - actor->x;
    actor->frame_dy[frame] = obj->sy - actor->y;
    actor->frame_hfl_mode[frame] = obj->hfl ? 2 : 1;
    actor->frame_vfl_mode[frame] = obj->vfl ? 2 : 1;
    runtime_actor_status("Current actor frame updated from selected object");
}

static bool runtime_actor_same_group(const RuntimeStageActor *a, const RuntimeStageActor *b)
{
    if (!a || !b) return false;
    if (a == b) return true;
    if (!a->sync_group[0]) return false;
    return strcmp(a->sync_group, b->sync_group) == 0;
}

static int runtime_actor_group_count(const RuntimeStageActor *actor)
{
    if (!actor) return 0;
    int count = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        if (runtime_actor_same_group(actor, &g_runtime_actors[i])) count++;
    }
    return count;
}

static int runtime_actor_group_leader_count(const RuntimeStageActor *actor)
{
    if (!actor || !actor->sync_group[0]) return 0;
    int count = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        if (runtime_actor_same_group(actor, a) && strcmp(a->group_role, "leader") == 0) count++;
    }
    return count;
}

static void runtime_actor_nudge_group(RuntimeStageActor *actor, int dx, int dy)
{
    if (!actor || (!dx && !dy)) return;
    int changed = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        RuntimeStageActor *a = &g_runtime_actors[i];
        if (!runtime_actor_same_group(actor, a)) continue;
        a->x += dx;
        a->y += dy;
        changed++;
    }
    char msg[128];
    snprintf(msg, sizeof msg, "Nudged %d runtime actor(s)", changed);
    runtime_actor_status(msg);
}

static void runtime_actor_select_group(RuntimeStageActor *actor)
{
    if (!actor) return;
    editor_project_clear_selection();
    int selected = 0;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        RuntimeStageActor *a = &g_runtime_actors[i];
        if (!runtime_actor_same_group(actor, a)) continue;
        if (a->source_object >= 0 && a->source_object < g_no) {
            g_sel_flags[a->source_object] = 1;
            g_hl_obj = a->source_object;
            selected++;
        }
    }
    g_view_changed = 1;
    char msg[128];
    snprintf(msg, sizeof msg, "Selected %d source object(s) in group", selected);
    runtime_actor_status(msg);
}

static void runtime_actor_copy_group_bgnd_notes(const RuntimeStageActor *actor)
{
    if (!actor) return;
    char out[16384];
    out[0] = '\0';
    const char *group = actor->sync_group[0] ? actor->sync_group : actor->name;
    runtime_actor_appendf(out, sizeof out, "; Runtime actor group: %s\n", group);
    runtime_actor_appendf(out, sizeof out, "; Generated from bddtool sidecar tuning. Verify driver/list choices before commit.\n\n");
    runtime_actor_appendf(out, sizeof out, "%s_calla\n", group);
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        if (!runtime_actor_same_group(actor, a)) continue;
        char label[96];
        runtime_actor_make_code_label(a->code_label[0] ? a->code_label : a->name, label, sizeof label);
        runtime_actor_appendf(out, sizeof out, "\tcreate\tpid_bani,%s_proc\t; %s part %d/%d\n",
                              label, a->group_role, a->part_index + 1, a->part_count);
    }
    runtime_actor_appendf(out, sizeof out, "\trets\n\n");
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        if (!runtime_actor_same_group(actor, a)) continue;
        char label[96];
        runtime_actor_make_code_label(a->code_label[0] ? a->code_label : a->name, label, sizeof label);
        runtime_actor_appendf(out, sizeof out, "%s_ani\n", label);
        for (int fi = 0; fi < a->frame_count; fi++) {
            runtime_actor_appendf(out, sizeof out, "\t.long\t%s\t; dx=%d dy=%d ticks=%d hfl=%d vfl=%d\n",
                                  a->frames[fi][0] ? a->frames[fi] : "0", a->frame_dx[fi], a->frame_dy[fi],
                                  runtime_actor_frame_ticks_at(a, fi), a->frame_hfl_mode[fi], a->frame_vfl_mode[fi]);
        }
        runtime_actor_appendf(out, sizeof out, "\t.long\tani_jump,%s_ani\n\n", label);
    }
    ImGui::SetClipboardText(out);
    stage_set_toast("Copied group BGND skeleton");
}

static void runtime_actor_duplicate_current_frame(RuntimeStageActor *actor)
{
    if (!actor || actor->frame_count <= 0) return;
    if (actor->frame_count >= MAX_RUNTIME_ACTOR_FRAMES) {
        runtime_actor_status("Frame list is full");
        return;
    }
    int frame = runtime_actor_current_frame(actor);
    if (frame < 0) frame = 0;
    if (frame >= actor->frame_count) frame = actor->frame_count - 1;
    for (int i = actor->frame_count; i > frame + 1; i--) {
        snprintf(actor->frames[i], sizeof actor->frames[i], "%s", actor->frames[i - 1]);
        actor->frame_dx[i] = actor->frame_dx[i - 1];
        actor->frame_dy[i] = actor->frame_dy[i - 1];
        actor->frame_ticks_override[i] = actor->frame_ticks_override[i - 1];
        actor->frame_hfl_mode[i] = actor->frame_hfl_mode[i - 1];
        actor->frame_vfl_mode[i] = actor->frame_vfl_mode[i - 1];
    }
    snprintf(actor->frames[frame + 1], sizeof actor->frames[frame + 1], "%s", actor->frames[frame]);
    actor->frame_dx[frame + 1] = actor->frame_dx[frame];
    actor->frame_dy[frame + 1] = actor->frame_dy[frame];
    actor->frame_ticks_override[frame + 1] = actor->frame_ticks_override[frame];
    actor->frame_hfl_mode[frame + 1] = actor->frame_hfl_mode[frame];
    actor->frame_vfl_mode[frame + 1] = actor->frame_vfl_mode[frame];
    actor->frame_count++;
    g_runtime_actor_timeline_paused = true;
    g_runtime_actor_scrub_frame = frame + 1;
    runtime_actor_status("Duplicated current actor frame");
}

static void runtime_actor_copy_current_frame_offsets(RuntimeStageActor *actor)
{
    if (!actor || actor->frame_count <= 0) return;
    int frame = runtime_actor_current_frame(actor);
    if (frame < 0 || frame >= actor->frame_count) return;
    g_runtime_actor_frame_clip_dx = actor->frame_dx[frame];
    g_runtime_actor_frame_clip_dy = actor->frame_dy[frame];
    g_runtime_actor_frame_clip_ticks = actor->frame_ticks_override[frame];
    g_runtime_actor_frame_clip_hfl = actor->frame_hfl_mode[frame];
    g_runtime_actor_frame_clip_vfl = actor->frame_vfl_mode[frame];
    g_runtime_actor_frame_clip_valid = true;
    runtime_actor_status("Copied current frame offsets");
}

static void runtime_actor_paste_current_frame_offsets(RuntimeStageActor *actor)
{
    if (!actor || !g_runtime_actor_frame_clip_valid || actor->frame_count <= 0) return;
    int frame = runtime_actor_current_frame(actor);
    if (frame < 0 || frame >= actor->frame_count) return;
    actor->frame_dx[frame] = g_runtime_actor_frame_clip_dx;
    actor->frame_dy[frame] = g_runtime_actor_frame_clip_dy;
    actor->frame_ticks_override[frame] = g_runtime_actor_frame_clip_ticks;
    actor->frame_hfl_mode[frame] = g_runtime_actor_frame_clip_hfl;
    actor->frame_vfl_mode[frame] = g_runtime_actor_frame_clip_vfl;
    runtime_actor_status("Pasted current frame offsets");
}

static void runtime_actor_apply_current_offsets_to_all(RuntimeStageActor *actor)
{
    if (!actor || actor->frame_count <= 0) return;
    int frame = runtime_actor_current_frame(actor);
    if (frame < 0 || frame >= actor->frame_count) return;
    for (int i = 0; i < actor->frame_count; i++) {
        actor->frame_dx[i] = actor->frame_dx[frame];
        actor->frame_dy[i] = actor->frame_dy[frame];
        actor->frame_ticks_override[i] = actor->frame_ticks_override[frame];
        actor->frame_hfl_mode[i] = actor->frame_hfl_mode[frame];
        actor->frame_vfl_mode[i] = actor->frame_vfl_mode[frame];
    }
    runtime_actor_status("Applied current frame offsets to all frames");
}

static void runtime_actor_clear_frame_offsets(RuntimeStageActor *actor)
{
    if (!actor) return;
    for (int i = 0; i < actor->frame_count; i++) {
        actor->frame_dx[i] = 0;
        actor->frame_dy[i] = 0;
        actor->frame_ticks_override[i] = 0;
        actor->frame_hfl_mode[i] = 0;
        actor->frame_vfl_mode[i] = 0;
    }
    runtime_actor_status("Cleared frame offsets");
}

static void runtime_actor_draw_validation(RuntimeStageActor *actor)
{
    if (!actor) return;
    int warnings = 0;
    for (int i = 0; i < actor->frame_count; i++) {
        if (runtime_actor_frame_image_index(actor, i) < 0) warnings++;
    }
    if (actor->replace_source && (actor->source_object < 0 || actor->source_object >= g_no)) warnings++;
    if (actor->sync_group_timing && !actor->sync_group[0]) warnings++;
    int leaders = runtime_actor_group_leader_count(actor);
    if (actor->sync_group[0] && leaders != 1) warnings++;
    if (actor->multipart_piece && actor->part_count <= 1) warnings++;

    if (warnings <= 0) {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Validation: OK");
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Validation: %d warning(s)", warnings);
    for (int i = 0; i < actor->frame_count; i++) {
        if (runtime_actor_frame_image_index(actor, i) < 0)
            ImGui::TextWrapped("Frame %d image label is missing or not imported: %s", i, actor->frames[i]);
    }
    if (actor->replace_source && (actor->source_object < 0 || actor->source_object >= g_no))
        ImGui::TextWrapped("Replace-source is enabled, but source object is missing.");
    if (actor->sync_group_timing && !actor->sync_group[0])
        ImGui::TextWrapped("Sync Group Timing is enabled without a Sync Group name.");
    if (actor->sync_group[0] && leaders != 1)
        ImGui::TextWrapped("Sync group should have exactly one leader; found %d.", leaders);
    if (actor->multipart_piece && actor->part_count <= 1)
        ImGui::TextWrapped("Multipart Piece is enabled, but Part Count is not greater than 1.");
}

static void runtime_actor_select_source_object(RuntimeStageActor *actor)
{
    if (!actor || actor->source_object < 0 || actor->source_object >= g_no) return;
    editor_project_clear_selection();
    g_sel_flags[actor->source_object] = 1;
    g_hl_obj = actor->source_object;
    g_view_changed = 1;
}

static void runtime_actor_project_base(const RuntimeStageActor *actor,
                                       int *out_x, int *out_y, float *out_scroll)
{
    int x = actor ? actor->x : 0;
    int y = actor ? actor->y : 0;
    float scroll = actor ? actor->scroll : 1.0f;

    if (actor && actor->source_object >= 0 && actor->source_object < g_no) {
        const Obj *src = &g_obj[actor->source_object];
        int src_x = src->depth;
        int src_y = src->sy;
        int dx = actor->x - src_x;
        int dy = actor->y - src_y;
        if (g_game_view) {
            bdd_object_game_origin(actor->source_object, &x, &y);
            scroll = bdd_object_game_scroll_factor(actor->source_object);
        } else if (g_runtime_layout_view) {
            bdd_object_editor_origin(actor->source_object, &x, &y);
        } else {
            x = src_x;
            y = src_y;
        }
        x += dx;
        y += dy;
    }

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_scroll) *out_scroll = scroll;
}

bool runtime_actor_preview_hides_object(int obj_i)
{
    if (!g_runtime_actor_preview || obj_i < 0) return false;
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        if (!a->enabled || !a->replace_source) continue;
        if (a->source_object == obj_i) return true;
    }
    return false;
}

static void runtime_actor_write_int_array(FILE *f, const char *key, const int *values, int count, bool trailing_comma)
{
    fprintf(f, "      \"%s\": [", key);
    for (int i = 0; i < count; i++) {
        if (i) fprintf(f, ", ");
        fprintf(f, "%d", values ? values[i] : 0);
    }
    fprintf(f, "]%s\n", trailing_comma ? "," : "");
}

bool runtime_actor_sidecar_save(void)
{
    char path[640];
    runtime_actor_default_sidecar_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) {
        runtime_actor_status("Could not write runtime sidecar");
        return false;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 3,\n");
    fprintf(f, "  \"source_type\": \"bddview_runtime_actors\",\n");
    fprintf(f, "  \"stage\": "); json_write_string(f, g_name); fprintf(f, ",\n");
    fprintf(f, "  \"actors\": [\n");
    for (int i = 0; i < g_runtime_actor_count; i++) {
        const RuntimeStageActor *a = &g_runtime_actors[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": "); json_write_string(f, a->name); fprintf(f, ",\n");
        fprintf(f, "      \"enabled\": %s,\n", a->enabled ? "true" : "false");
        fprintf(f, "      \"trigger\": "); json_write_string(f, a->trigger); fprintf(f, ",\n");
        fprintf(f, "      \"emit_bgnd_code\": %s,\n", a->emit_bgnd_code ? "true" : "false");
        fprintf(f, "      \"code_pattern\": "); json_write_string(f, a->code_pattern); fprintf(f, ",\n");
        fprintf(f, "      \"code_label\": "); json_write_string(f, a->code_label); fprintf(f, ",\n");
        fprintf(f, "      \"insert_list\": "); json_write_string(f, a->insert_list); fprintf(f, ",\n");
        fprintf(f, "      \"scroll_symbol\": "); json_write_string(f, a->scroll_symbol); fprintf(f, ",\n");
        fprintf(f, "      \"sync_group\": "); json_write_string(f, a->sync_group); fprintf(f, ",\n");
        fprintf(f, "      \"frame_driver\": "); json_write_string(f, a->frame_driver); fprintf(f, ",\n");
        fprintf(f, "      \"group_role\": "); json_write_string(f, a->group_role); fprintf(f, ",\n");
        fprintf(f, "      \"group_index\": %d,\n", a->group_index);
        fprintf(f, "      \"sync_group_timing\": %s,\n", a->sync_group_timing ? "true" : "false");
        fprintf(f, "      \"multipart_piece\": %s,\n", a->multipart_piece ? "true" : "false");
        fprintf(f, "      \"part_index\": %d,\n", a->part_index);
        fprintf(f, "      \"part_count\": %d,\n", a->part_count);
        fprintf(f, "      \"parent_actor\": "); json_write_string(f, a->parent_actor); fprintf(f, ",\n");
        fprintf(f, "      \"source_object\": %d,\n", a->source_object);
        fprintf(f, "      \"replace_source\": %s,\n", a->replace_source ? "true" : "false");
        fprintf(f, "      \"screen_space_y\": %s,\n", a->screen_space_y ? "true" : "false");
        fprintf(f, "      \"x\": %d,\n", a->x);
        fprintf(f, "      \"y\": %d,\n", a->y);
        fprintf(f, "      \"layer\": %d,\n", a->layer);
        fprintf(f, "      \"scroll\": %.4f,\n", a->scroll);
        fprintf(f, "      \"frame_ticks\": %d,\n", a->frame_ticks);
        fprintf(f, "      \"phase_ticks\": %d,\n", a->phase_ticks);
        fprintf(f, "      \"delay_min_ticks\": %d,\n", a->delay_min_ticks);
        fprintf(f, "      \"delay_max_ticks\": %d,\n", a->delay_max_ticks);
        fprintf(f, "      \"motion_x\": %d,\n", a->motion_x);
        fprintf(f, "      \"motion_y\": %d,\n", a->motion_y);
        fprintf(f, "      \"loop\": %s,\n", a->loop ? "true" : "false");
        fprintf(f, "      \"hfl\": %s,\n", a->hfl ? "true" : "false");
        fprintf(f, "      \"vfl\": %s,\n", a->vfl ? "true" : "false");
        fprintf(f, "      \"frames\": [");
        for (int fi = 0; fi < a->frame_count; fi++) {
            if (fi) fprintf(f, ", ");
            json_write_string(f, a->frames[fi]);
        }
        fprintf(f, "],\n");
        runtime_actor_write_int_array(f, "frame_dx", a->frame_dx, a->frame_count, true);
        runtime_actor_write_int_array(f, "frame_dy", a->frame_dy, a->frame_count, true);
        runtime_actor_write_int_array(f, "frame_ticks_override", a->frame_ticks_override, a->frame_count, true);
        runtime_actor_write_int_array(f, "frame_hfl_mode", a->frame_hfl_mode, a->frame_count, true);
        runtime_actor_write_int_array(f, "frame_vfl_mode", a->frame_vfl_mode, a->frame_count, false);
        fprintf(f, "    }%s\n", (i + 1 < g_runtime_actor_count) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status, "Saved %d runtime actor(s): %s", g_runtime_actor_count, path);
    stage_set_toast("Saved runtime actor sidecar");
    return true;
}

static const char *find_matching_brace(const char *start)
{
    if (!start || *start != '{') return NULL;
    bool in_string = false;
    bool esc = false;
    int depth = 0;
    for (const char *p = start; *p; p++) {
        if (in_string) {
            if (esc) esc = false;
            else if (*p == '\\') esc = true;
            else if (*p == '"') in_string = false;
            continue;
        }
        if (*p == '"') { in_string = true; continue; }
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) return p;
        }
    }
    return NULL;
}

static int parse_json_string_array(const char *json, const char *key,
                                   char out[MAX_RUNTIME_ACTOR_FRAMES][64])
{
    char pattern[96];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && count < MAX_RUNTIME_ACTOR_FRAMES) {
        while (*p && *p != '"' && *p != ']') p++;
        if (*p == ']') break;
        p++;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < sizeof out[0]) {
            if (*p == '\\' && p[1]) p++;
            out[count][n++] = *p++;
        }
        out[count][n] = '\0';
        if (*p == '"') p++;
        count++;
    }
    return count;
}

static int parse_json_int_array(const char *json, const char *key, int out[MAX_RUNTIME_ACTOR_FRAMES])
{
    if (!out) return 0;
    char pattern[96];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && count < MAX_RUNTIME_ACTOR_FRAMES) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        char *end = NULL;
        long v = strtol(p, &end, 0);
        if (end == p) break;
        out[count++] = (int)v;
        p = end;
    }
    return count;
}

bool runtime_actor_sidecar_load(void)
{
    char path[640];
    runtime_actor_default_sidecar_path(path, sizeof path);
    if (!stage_file_exists(path)) {
        snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status, "No runtime sidecar found: %s", path);
        return false;
    }
    char *json = stage_read_text_file(path);
    if (!json) {
        runtime_actor_status("Could not read runtime sidecar");
        return false;
    }

    RuntimeStageActor loaded[MAX_RUNTIME_ACTORS] = {};
    int loaded_count = 0;
    const char *actors = strstr(json, "\"actors\"");
    if (actors) actors = strchr(actors, '[');
    const char *p = actors ? actors + 1 : NULL;
    while (p && *p && *p != ']' && loaded_count < MAX_RUNTIME_ACTORS) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p == ']' || !*p) break;
        const char *end = find_matching_brace(p);
        if (!end) break;
        size_t len = (size_t)(end - p + 1);
        if (len > 8191) len = 8191;
        char obj[8192];
        memcpy(obj, p, len);
        obj[len] = '\0';

        RuntimeStageActor a = {};
        snprintf(a.name, sizeof a.name, "actor_%02d", loaded_count + 1);
        snprintf(a.trigger, sizeof a.trigger, "always");
        snprintf(a.code_pattern, sizeof a.code_pattern, "loop_sprite");
        snprintf(a.insert_list, sizeof a.insert_list, "baklst4");
        snprintf(a.scroll_symbol, sizeof a.scroll_symbol, "worldtlx4");
        snprintf(a.frame_driver, sizeof a.frame_driver, "frame_a9");
        snprintf(a.group_role, sizeof a.group_role, "solo");
        snprintf(a.parent_actor, sizeof a.parent_actor, "");
        a.source_object = -1;
        a.frame_ticks = 6;
        a.loop = true;
        a.enabled = true;
        a.scroll = 1.0f;
        a.emit_bgnd_code = true;
        a.part_count = 1;
        a.sync_group_timing = false;
        a.multipart_piece = false;
        a.screen_space_y = true;
        json_get_string_value(obj, "name", a.name, sizeof a.name);
        json_get_string_value(obj, "trigger", a.trigger, sizeof a.trigger);
        json_get_bool_value(obj, "emit_bgnd_code", &a.emit_bgnd_code);
        json_get_string_value(obj, "code_pattern", a.code_pattern, sizeof a.code_pattern);
        json_get_string_value(obj, "code_label", a.code_label, sizeof a.code_label);
        json_get_string_value(obj, "insert_list", a.insert_list, sizeof a.insert_list);
        json_get_string_value(obj, "scroll_symbol", a.scroll_symbol, sizeof a.scroll_symbol);
        json_get_string_value(obj, "sync_group", a.sync_group, sizeof a.sync_group);
        json_get_string_value(obj, "frame_driver", a.frame_driver, sizeof a.frame_driver);
        json_get_string_value(obj, "group_role", a.group_role, sizeof a.group_role);
        json_get_int_value(obj, "group_index", &a.group_index);
        json_get_bool_value(obj, "sync_group_timing", &a.sync_group_timing);
        json_get_bool_value(obj, "multipart_piece", &a.multipart_piece);
        json_get_int_value(obj, "part_index", &a.part_index);
        json_get_int_value(obj, "part_count", &a.part_count);
        json_get_string_value(obj, "parent_actor", a.parent_actor, sizeof a.parent_actor);
        json_get_int_value(obj, "source_object", &a.source_object);
        json_get_bool_value(obj, "replace_source", &a.replace_source);
        bool screen_space_y = a.source_object < 0;
        if (json_get_bool_value(obj, "screen_space_y", &screen_space_y))
            a.screen_space_y = screen_space_y;
        else
            a.screen_space_y = a.source_object < 0;
        json_get_int_value(obj, "x", &a.x);
        json_get_int_value(obj, "y", &a.y);
        json_get_int_value(obj, "layer", &a.layer);
        json_get_float_value(obj, "scroll", &a.scroll);
        json_get_int_value(obj, "frame_ticks", &a.frame_ticks);
        json_get_int_value(obj, "phase_ticks", &a.phase_ticks);
        json_get_int_value(obj, "delay_min_ticks", &a.delay_min_ticks);
        json_get_int_value(obj, "delay_max_ticks", &a.delay_max_ticks);
        json_get_int_value(obj, "motion_x", &a.motion_x);
        json_get_int_value(obj, "motion_y", &a.motion_y);
        json_get_bool_value(obj, "loop", &a.loop);
        json_get_bool_value(obj, "enabled", &a.enabled);
        bool hfl = false, vfl = false;
        if (json_get_bool_value(obj, "hfl", &hfl)) a.hfl = hfl ? 1 : 0;
        if (json_get_bool_value(obj, "vfl", &vfl)) a.vfl = vfl ? 1 : 0;
        a.frame_count = parse_json_string_array(obj, "frames", a.frames);
        parse_json_int_array(obj, "frame_dx", a.frame_dx);
        parse_json_int_array(obj, "frame_dy", a.frame_dy);
        parse_json_int_array(obj, "frame_ticks_override", a.frame_ticks_override);
        parse_json_int_array(obj, "frame_hfl_mode", a.frame_hfl_mode);
        parse_json_int_array(obj, "frame_vfl_mode", a.frame_vfl_mode);
        if (a.frame_count <= 0) {
            char frame[64] = "";
            if (json_get_string_value(obj, "asset", frame, sizeof frame) && frame[0]) {
                snprintf(a.frames[0], sizeof a.frames[0], "%s", frame);
                a.frame_count = 1;
            }
        }
        if (a.frame_count <= 0) snprintf(a.frames[a.frame_count++], sizeof a.frames[0], "");
        if (a.frame_ticks < 1) a.frame_ticks = 1;
        if (!a.code_label[0]) runtime_actor_make_code_label(a.name, a.code_label, sizeof a.code_label);
        if (!a.code_pattern[0]) snprintf(a.code_pattern, sizeof a.code_pattern, "loop_sprite");
        if (!a.insert_list[0]) snprintf(a.insert_list, sizeof a.insert_list, "baklst4");
        if (!a.scroll_symbol[0]) snprintf(a.scroll_symbol, sizeof a.scroll_symbol, "worldtlx4");
        if (!a.frame_driver[0]) snprintf(a.frame_driver, sizeof a.frame_driver, "frame_a9");
        if (!a.group_role[0]) snprintf(a.group_role, sizeof a.group_role, "solo");
        if (a.part_count < 1) a.part_count = 1;
        loaded[loaded_count++] = a;
        p = end + 1;
    }
    free(json);

    memcpy(g_runtime_actors, loaded, sizeof(RuntimeStageActor) * (size_t)loaded_count);
    g_runtime_actor_count = loaded_count;
    runtime_actor_select(loaded_count > 0 ? 0 : -1);
    g_runtime_actor_preview = loaded_count > 0;
    snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status, "Loaded %d runtime actor(s): %s", loaded_count, path);
    if (loaded_count > 0) stage_set_toast("Loaded runtime actor sidecar");
    return loaded_count > 0;
}

int runtime_actor_import_inferred_level_animations(void)
{
    InferredRuntimeFrameGroup groups[MAX_INFERRED_RUNTIME_GROUPS] = {};
    int group_count = runtime_actor_collect_inferred_groups(groups, MAX_INFERRED_RUNTIME_GROUPS);
    if (group_count <= 0) {
        snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status,
                 "No inferred IMG animation frame groups found");
        return 0;
    }

    int added = 0;
    for (int oi = 0; oi < g_no && g_runtime_actor_count < MAX_RUNTIME_ACTORS; oi++) {
        if (runtime_actor_has_source_object_actor(oi)) continue;
        Obj *obj = &g_obj[oi];
        Img *source_im = img_find(obj->ii);
        if (!source_im) continue;
        int source_img_i = (int)(source_im - g_img);

        int best_group = -1;
        int source_frame = -1;
        for (int gi = 0; gi < group_count; gi++) {
            int fi = runtime_actor_group_contains_image(&groups[gi], source_img_i);
            if (fi >= 0) {
                best_group = gi;
                source_frame = fi;
                break;
            }
        }
        if (best_group < 0) continue;

        InferredRuntimeFrameGroup *g = &groups[best_group];
        RuntimeStageActor actor;
        runtime_actor_init_default(&actor);

        snprintf(actor.name, sizeof actor.name, "%s_auto_%03d",
                 g->base[0] ? g->base : "anim", oi);
        runtime_actor_make_code_label(actor.name, actor.code_label, sizeof actor.code_label);
        snprintf(actor.trigger, sizeof actor.trigger, "autoload");
        actor.source_object = oi;
        actor.replace_source = true;
        actor.screen_space_y = false;
        actor.x = obj->depth;
        actor.y = obj->sy;
        actor.layer = (obj->wx >> 8) & 0xFF;
        actor.scroll = mk2_scroll_factor_for_layer(actor.layer);
        actor.hfl = obj->hfl ? 1 : 0;
        actor.vfl = obj->vfl ? 1 : 0;
        actor.phase_ticks = source_frame > 0 ? source_frame * actor.frame_ticks : 0;
        actor.frame_count = g->frame_count;

        int source_ax = img_anim_offset_x(source_im, actor.hfl);
        int source_ay = img_anim_offset_y(source_im, actor.vfl);
        for (int fi = 0; fi < actor.frame_count; fi++) {
            Img *frame_im = &g_img[g->frames[fi]];
            runtime_actor_image_label(frame_im, actor.frames[fi], sizeof actor.frames[fi]);
            actor.frame_dx[fi] = source_ax - img_anim_offset_x(frame_im, actor.hfl);
            actor.frame_dy[fi] = source_ay - img_anim_offset_y(frame_im, actor.vfl);
            actor.frame_ticks_override[fi] = 0;
            actor.frame_hfl_mode[fi] = 0;
            actor.frame_vfl_mode[fi] = 0;
        }

        g_runtime_actors[g_runtime_actor_count++] = actor;
        added++;
    }

    if (added > 0) {
        runtime_actor_select(0);
        g_runtime_actor_preview = true;
        snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status,
                 "Auto-imported %d inferred animation actor(s) from %d IMG frame group(s)",
                 added, group_count);
        stage_set_toast("Auto-imported level animations");
    } else {
        snprintf(g_runtime_actor_status, sizeof g_runtime_actor_status,
                 "Found IMG animation frame groups, but none match placed BDB objects");
    }
    return added;
}

void runtime_actor_autoload_for_stage(void)
{
    g_runtime_actor_count = 0;
    g_runtime_actor_selected = -1;
    g_runtime_actor_preview = false;
    if (!runtime_actor_preview_imports_loaded()) {
        g_runtime_actor_preview_base_images = -1;
        g_runtime_actor_preview_base_pals = -1;
    } else if (g_runtime_actor_preview_base_images < 0 ||
               g_runtime_actor_preview_base_images >= g_ni ||
               !runtime_actor_image_is_preview_import(&g_img[g_runtime_actor_preview_base_images])) {
        g_runtime_actor_preview_base_images = runtime_actor_first_preview_import();
        g_runtime_actor_preview_base_pals = -1;
    }
    runtime_actor_import_stage_runtime_art();
    if (!runtime_actor_sidecar_load()) {
        int known = runtime_actor_add_forest_tree_actors();
        if (known <= 0)
            runtime_actor_import_inferred_level_animations();
    }
}

void draw_mk2_runtime_actor_overlay(void)
{
    if (!g_runtime_actor_preview || g_runtime_actor_count <= 0 || !g_have_bdb || g_zoom <= 0)
        return;
    if (!g_game_view && !g_runtime_layout_view) return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    BddScreenRect viewport = {0, 0, (int)ds.x, (int)ds.y};
    if (g_game_view) {
        bdd_game_view_screen_rect(g_zoom, (int)ds.x, (int)ds.y, &viewport);
        dl->PushClipRect(ImVec2((float)viewport.x, (float)viewport.y),
                         ImVec2((float)(viewport.x + viewport.w),
                                (float)(viewport.y + viewport.h)), true);
    }

    for (int i = 0; i < g_runtime_actor_count; i++) {
        RuntimeStageActor *a = &g_runtime_actors[i];
        if (!a->enabled || a->frame_count <= 0) continue;
        int frame = runtime_actor_current_frame(a);
        int img_i = runtime_actor_frame_image_index(a, frame);
        if (img_i < 0 || img_i >= g_ni) continue;
        Img *im = &g_img[img_i];
        SDL_Texture *tex = editor_texture_at(img_i);
        if (!tex || !im || im->w <= 0 || im->h <= 0) continue;

        float sx, sy;
        int base_x = 0;
        int base_y = 0;
        float actor_scroll = 1.0f;
        runtime_actor_project_base(a, &base_x, &base_y, &actor_scroll);
        int draw_x = base_x + a->frame_dx[frame];
        int draw_y = base_y + a->frame_dy[frame];
        if (g_game_view) {
            int screen_y = a->screen_space_y ? draw_y : (draw_y - g_game_view_y);
            sx = (float)viewport.x + ((float)draw_x - (float)g_scroll_pos * actor_scroll) * (float)g_zoom;
            sy = (float)viewport.y + (float)screen_y * (float)g_zoom;
            if (sx > viewport.x + viewport.w + 200 || sy > viewport.y + viewport.h + 200 ||
                sx + im->w * g_zoom < viewport.x - 200 || sy + im->h * g_zoom < viewport.y - 200)
                continue;
        } else {
            sx = ((float)draw_x - (float)g_view_x) * (float)g_zoom;
            sy = ((float)draw_y - (float)g_view_y) * (float)g_zoom;
        }
        ImVec2 p0(sx, sy);
        ImVec2 p1(sx + (float)im->w * (float)g_zoom,
                  sy + (float)im->h * (float)g_zoom);
        bool frame_hfl = runtime_actor_frame_hfl_at(a, frame);
        bool frame_vfl = runtime_actor_frame_vfl_at(a, frame);
        ImVec2 uv0(frame_hfl ? 1.0f : 0.0f, frame_vfl ? 1.0f : 0.0f);
        ImVec2 uv1(frame_hfl ? 0.0f : 1.0f, frame_vfl ? 0.0f : 1.0f);
        if (g_runtime_actor_onion_skin && a->frame_count > 1) {
            int onion_frames[2] = { frame - 1, frame + 1 };
            for (int oi = 0; oi < 2; oi++) {
                int of = onion_frames[oi];
                if (of < 0) of = a->loop ? a->frame_count - 1 : 0;
                if (of >= a->frame_count) of = a->loop ? 0 : a->frame_count - 1;
                if (of == frame) continue;
                int oimg_i = runtime_actor_frame_image_index(a, of);
                if (oimg_i < 0 || oimg_i >= g_ni) continue;
                Img *oim = &g_img[oimg_i];
                SDL_Texture *otex = editor_texture_at(oimg_i);
                if (!otex || !oim || oim->w <= 0 || oim->h <= 0) continue;
                int odraw_x = base_x + a->frame_dx[of];
                int odraw_y = base_y + a->frame_dy[of];
                float osx, osy;
                if (g_game_view) {
                    int oscreen_y = a->screen_space_y ? odraw_y : (odraw_y - g_game_view_y);
                    osx = (float)viewport.x + ((float)odraw_x - (float)g_scroll_pos * actor_scroll) * (float)g_zoom;
                    osy = (float)viewport.y + (float)oscreen_y * (float)g_zoom;
                } else {
                    osx = ((float)odraw_x - (float)g_view_x) * (float)g_zoom;
                    osy = ((float)odraw_y - (float)g_view_y) * (float)g_zoom;
                }
                ImVec2 op0(osx, osy);
                ImVec2 op1(osx + (float)oim->w * (float)g_zoom, osy + (float)oim->h * (float)g_zoom);
                bool ohfl = runtime_actor_frame_hfl_at(a, of);
                bool ovfl = runtime_actor_frame_vfl_at(a, of);
                ImVec2 ouv0(ohfl ? 1.0f : 0.0f, ovfl ? 1.0f : 0.0f);
                ImVec2 ouv1(ohfl ? 0.0f : 1.0f, ovfl ? 0.0f : 1.0f);
                ImU32 tint = oi == 0 ? IM_COL32(120, 170, 255, 72) : IM_COL32(255, 190, 90, 72);
                dl->AddImage((ImTextureID)(intptr_t)otex, op0, op1, ouv0, ouv1, tint);
                if (g_show_borders)
                    dl->AddRect(op0, op1, tint, 0.0f, 0, 1.0f);
            }
        }
        dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1, uv0, uv1, IM_COL32_WHITE);
        if (g_show_borders && g_runtime_actor_labels) {
            dl->AddRect(p0, p1, IM_COL32(80, 220, 255, 190), 0.0f, 0, 1.5f);
            char label[128];
            if (a->sync_group[0] || a->multipart_piece)
                snprintf(label, sizeof label, "%s  %d/%d  %s[%d/%d]", a->name, frame + 1, a->frame_count,
                         a->sync_group[0] ? a->sync_group : a->group_role, a->part_index + 1, a->part_count);
            else
                snprintf(label, sizeof label, "%s  %d/%d", a->name, frame + 1, a->frame_count);
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddRectFilled(ImVec2(p0.x, p0.y - ts.y - 5.0f),
                              ImVec2(p0.x + ts.x + 8.0f, p0.y),
                              IM_COL32(0, 0, 0, 170), 2.0f);
            dl->AddText(ImVec2(p0.x + 4.0f, p0.y - ts.y - 3.0f),
                        IM_COL32(150, 235, 255, 230), label);
        }
    }
    if (g_game_view)
        dl->PopClipRect();
}

void draw_mk2_runtime_actor_isolation_window(void)
{
    if (!g_runtime_actor_isolation_open) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Runtime Animation Isolate##rtactor_iso",
                      &g_runtime_actor_isolation_open)) {
        ImGui::End();
        return;
    }

    if (g_runtime_actor_count <= 0) {
        ImGui::TextDisabled("No runtime actors loaded.");
        ImGui::End();
        return;
    }
    if (g_runtime_actor_selected < 0 || g_runtime_actor_selected >= g_runtime_actor_count)
        runtime_actor_select(0);

    const char *combo = g_runtime_actors[g_runtime_actor_selected].name;
    if (ImGui::BeginCombo("Actor", combo)) {
        for (int i = 0; i < g_runtime_actor_count; i++) {
            char label[96];
            snprintf(label, sizeof label, "%02d  %s", i + 1, g_runtime_actors[i].name);
            if (ImGui::Selectable(label, i == g_runtime_actor_selected))
                runtime_actor_select(i);
        }
        ImGui::EndCombo();
    }

    RuntimeStageActor *actor = runtime_actor_selected_ptr();
    if (!actor) {
        ImGui::End();
        return;
    }

    int frame = runtime_actor_current_frame(actor);
    if (!g_runtime_actor_timeline_paused)
        g_runtime_actor_scrub_frame = frame;

    if (ImGui::Button(g_runtime_actor_timeline_paused ? "Play" : "Pause"))
        g_runtime_actor_timeline_paused = !g_runtime_actor_timeline_paused;
    ImGui::SameLine();
    if (ImGui::Button("Prev")) {
        g_runtime_actor_timeline_paused = true;
        g_runtime_actor_scrub_frame = (g_runtime_actor_scrub_frame + actor->frame_count - 1) % actor->frame_count;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
        g_runtime_actor_timeline_paused = true;
        g_runtime_actor_scrub_frame = (g_runtime_actor_scrub_frame + 1) % actor->frame_count;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &g_runtime_actor_onion_skin);
    ImGui::SliderInt("Frame", &g_runtime_actor_scrub_frame, 0, actor->frame_count - 1);
    ImGui::SliderFloat("Zoom", &g_runtime_actor_isolation_zoom, 0.5f, 8.0f, "%.1fx");
    if (g_runtime_actor_isolation_zoom < 0.5f) g_runtime_actor_isolation_zoom = 0.5f;

    frame = runtime_actor_current_frame(actor);
    int img_i = runtime_actor_frame_image_index(actor, frame);
    const char *frame_label = (frame >= 0 && frame < actor->frame_count) ? actor->frames[frame] : "";

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 240.0f) avail.x = 240.0f;
    if (avail.y < 260.0f) avail.y = 260.0f;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + avail.x, p0.y + avail.y);
    ImGui::InvisibleButton("##runtime_actor_isolate_canvas", avail);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(22, 24, 28, 255));
    const float cell = 16.0f;
    for (float y = p0.y; y < p1.y; y += cell) {
        for (float x = p0.x; x < p1.x; x += cell) {
            int ix = (int)((x - p0.x) / cell);
            int iy = (int)((y - p0.y) / cell);
            ImU32 c = ((ix + iy) & 1) ? IM_COL32(30, 33, 38, 255) : IM_COL32(38, 42, 48, 255);
            dl->AddRectFilled(ImVec2(x, y),
                              ImVec2(std::min(x + cell, p1.x), std::min(y + cell, p1.y)), c);
        }
    }
    dl->AddRect(p0, p1, IM_COL32(90, 105, 122, 255));

    if (img_i < 0 || img_i >= g_ni) {
        char msg[192];
        snprintf(msg, sizeof msg, "Missing imported frame image: %s", frame_label);
        dl->AddText(ImVec2(p0.x + 14.0f, p0.y + 14.0f), IM_COL32(255, 210, 120, 255), msg);
    } else {
        Img *im = &g_img[img_i];
        SDL_Texture *tex = editor_texture_at(img_i);
        if (im && tex && im->w > 0 && im->h > 0) {
            dl->PushClipRect(p0, p1, true);
            float zoom = g_runtime_actor_isolation_zoom;
            float w = (float)im->w * zoom;
            float h = (float)im->h * zoom;
            ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ip0(center.x - w * 0.5f, center.y - h * 0.5f);
            ImVec2 ip1(center.x + w * 0.5f, center.y + h * 0.5f);

            if (g_runtime_actor_onion_skin && actor->frame_count > 1) {
                int onion_frames[2] = { frame - 1, frame + 1 };
                for (int oi = 0; oi < 2; oi++) {
                    int of = onion_frames[oi];
                    if (of < 0) of = actor->loop ? actor->frame_count - 1 : 0;
                    if (of >= actor->frame_count) of = actor->loop ? 0 : actor->frame_count - 1;
                    int oimg_i = runtime_actor_frame_image_index(actor, of);
                    if (oimg_i < 0 || oimg_i >= g_ni) continue;
                    Img *oim = &g_img[oimg_i];
                    SDL_Texture *otex = editor_texture_at(oimg_i);
                    if (!oim || !otex || oim->w <= 0 || oim->h <= 0) continue;
                    float ow = (float)oim->w * zoom;
                    float oh = (float)oim->h * zoom;
                    ImVec2 op0(center.x - ow * 0.5f, center.y - oh * 0.5f);
                    ImVec2 op1(center.x + ow * 0.5f, center.y + oh * 0.5f);
                    ImU32 tint = oi == 0 ? IM_COL32(120, 170, 255, 82) : IM_COL32(255, 190, 90, 82);
                    dl->AddImage((ImTextureID)(intptr_t)otex, op0, op1, ImVec2(0, 0), ImVec2(1, 1), tint);
                    dl->AddRect(op0, op1, tint);
                }
            }

            bool frame_hfl = runtime_actor_frame_hfl_at(actor, frame);
            bool frame_vfl = runtime_actor_frame_vfl_at(actor, frame);
            ImVec2 uv0(frame_hfl ? 1.0f : 0.0f, frame_vfl ? 1.0f : 0.0f);
            ImVec2 uv1(frame_hfl ? 0.0f : 1.0f, frame_vfl ? 0.0f : 1.0f);
            dl->AddImage((ImTextureID)(intptr_t)tex, ip0, ip1, uv0, uv1, IM_COL32_WHITE);
            dl->AddRect(ip0, ip1, IM_COL32(130, 230, 255, 210), 0.0f, 0, 1.5f);
            dl->PopClipRect();
        }
    }

    ImGui::Text("%s  frame %d/%d  %s", actor->name, frame + 1, actor->frame_count,
                frame_label ? frame_label : "");
    ImGui::End();
}

static void runtime_actor_flip_mode_combo(const char *label, int *mode)
{
    if (!mode) return;
    int idx = *mode;
    if (idx < 0 || idx > 2) idx = 0;
    if (ImGui::BeginCombo(label, k_runtime_actor_flip_modes[idx])) {
        for (int i = 0; i < 3; i++) {
            bool selected = i == idx;
            if (ImGui::Selectable(k_runtime_actor_flip_modes[i], selected)) {
                *mode = i;
                idx = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

static void draw_runtime_actor_frame_row(RuntimeStageActor *actor, int frame)
{
    ImGui::PushID(frame);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%d", frame);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##frame_label", actor->frames[frame], sizeof actor->frames[frame]);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(48.0f);
    ImGui::InputInt("##dx", &actor->frame_dx[frame], 1, 8);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(48.0f);
    ImGui::InputInt("##dy", &actor->frame_dy[frame], 1, 8);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(48.0f);
    ImGui::InputInt("##ticks", &actor->frame_ticks_override[frame], 1, 4);
    if (actor->frame_ticks_override[frame] < 0) actor->frame_ticks_override[frame] = 0;
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(72.0f);
    runtime_actor_flip_mode_combo("##hfl", &actor->frame_hfl_mode[frame]);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(72.0f);
    runtime_actor_flip_mode_combo("##vfl", &actor->frame_vfl_mode[frame]);
    ImGui::TableNextColumn();
    int img_i = runtime_actor_frame_image_index(actor, frame);
    if (img_i >= 0 && img_i < g_ni) {
        Img *im = &g_img[img_i];
        ImGui::Text("0x%X %dx%d", im->idx, im->w, im->h);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f), "missing");
    }
    ImGui::PopID();
}

void draw_mk2_runtime_actor_tool(void)
{
    ImGui::Text("Runtime Animation Actors");
    ImGui::TextDisabled("Preview stage runtime animations from sidecars or inferred IMG frame groups.");

    char sidecar[640];
    runtime_actor_default_sidecar_path(sidecar, sizeof sidecar);
    ImGui::TextWrapped("Sidecar: %s", sidecar);
    ImGui::Checkbox("Preview actors", &g_runtime_actor_preview);
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &g_runtime_actor_labels);
    if (ImGui::Button("Open Isolated Animation Window", ImVec2(-1, 0)))
        g_runtime_actor_isolation_open = true;

    if (ImGui::Button("New Actor From Selected Object", ImVec2(-1, 0)))
        runtime_actor_add_from_selected_object();
    if (ImGui::Button("Load Runtime Sidecar", ImVec2(-1, 0)))
        runtime_actor_sidecar_load();
    if (ImGui::Button("Infer IMG Level Animations", ImVec2(-1, 0)))
        runtime_actor_import_inferred_level_animations();
    if (ImGui::Button("Save Runtime Sidecar", ImVec2(-1, 0)))
        runtime_actor_sidecar_save();
    if (ImGui::Button("Import Hanger Sprite IMGs", ImVec2(-1, 0)))
        runtime_actor_import_deadpool_imgs();
    if (runtime_actor_preview_imports_loaded()) {
        char preview_status[160];
        runtime_actor_preview_import_status(preview_status, sizeof preview_status);
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.24f, 1.0f), "%s", preview_status);
        ImGui::TextDisabled("Normal BDD save is blocked until these preview sprites are discarded.");
        if (ImGui::Button("Discard Preview IMG Imports", ImVec2(-1, 0)))
            runtime_actor_discard_preview_imports();
    }
    if (ImGui::Button("Copy Runtime JSON Path", ImVec2(-1, 0))) {
        ImGui::SetClipboardText(sidecar);
        stage_set_toast("Copied runtime sidecar path");
    }

    if (g_runtime_actor_count <= 0) {
        ImGui::Separator();
        ImGui::TextDisabled("No runtime actors yet. Select a placed object in runtime/game view, then create an actor.");
        if (g_runtime_actor_status[0]) ImGui::TextWrapped("%s", g_runtime_actor_status);
        return;
    }

    if (g_runtime_actor_selected < 0 || g_runtime_actor_selected >= g_runtime_actor_count)
        runtime_actor_select(0);

    const char *combo = g_runtime_actors[g_runtime_actor_selected].name;
    if (ImGui::BeginCombo("Actor", combo)) {
        for (int i = 0; i < g_runtime_actor_count; i++) {
            char label[96];
            snprintf(label, sizeof label, "%02d  %s", i + 1, g_runtime_actors[i].name);
            if (ImGui::Selectable(label, i == g_runtime_actor_selected))
                runtime_actor_select(i);
        }
        ImGui::EndCombo();
    }

    RuntimeStageActor *actor = runtime_actor_selected_ptr();
    if (!actor) return;

    int timeline_frame = runtime_actor_current_frame(actor);
    if (!g_runtime_actor_timeline_paused)
        g_runtime_actor_scrub_frame = timeline_frame;

    ImGui::Separator();
    ImGui::Text("Timeline");
    if (ImGui::Button(g_runtime_actor_timeline_paused ? "Play" : "Pause"))
        g_runtime_actor_timeline_paused = !g_runtime_actor_timeline_paused;
    ImGui::SameLine();
    if (ImGui::Button("Prev")) {
        g_runtime_actor_timeline_paused = true;
        g_runtime_actor_scrub_frame = (g_runtime_actor_scrub_frame + actor->frame_count - 1) % actor->frame_count;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
        g_runtime_actor_timeline_paused = true;
        g_runtime_actor_scrub_frame = (g_runtime_actor_scrub_frame + 1) % actor->frame_count;
    }
    ImGui::SliderInt("Scrub Frame", &g_runtime_actor_scrub_frame, 0, actor->frame_count - 1);
    ImGui::SliderFloat("Playback Speed", &g_runtime_actor_playback_speed, 0.10f, 3.00f, "%.2fx");
    if (g_runtime_actor_playback_speed < 0.10f) g_runtime_actor_playback_speed = 0.10f;
    ImGui::Checkbox("Onion Skin", &g_runtime_actor_onion_skin);

    ImGui::Separator();
    ImGui::InputText("Name", actor->name, sizeof actor->name);
    ImGui::InputText("Trigger", actor->trigger, sizeof actor->trigger);
    ImGui::Checkbox("Enabled", &actor->enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &actor->loop);
    ImGui::Checkbox("Replace source object in preview", &actor->replace_source);

    ImGui::InputInt("X", &actor->x);
    ImGui::InputInt("Y", &actor->y);
    ImGui::InputInt("Layer", &actor->layer);
    ImGui::InputFloat("Scroll", &actor->scroll, 0.05f, 0.25f, "%.4f");
    ImGui::Checkbox("Screen-space Y", &actor->screen_space_y);
    if (ImGui::Button("Use Layer Scroll", ImVec2(-1, 0)))
        actor->scroll = mk2_scroll_factor_for_layer(actor->layer);
    ImGui::InputInt("Frame Ticks", &actor->frame_ticks);
    ImGui::InputInt("Phase Ticks", &actor->phase_ticks);
    if (actor->frame_ticks < 1) actor->frame_ticks = 1;
    bool hfl = actor->hfl != 0;
    bool vfl = actor->vfl != 0;
    if (ImGui::Checkbox("HFlip", &hfl)) actor->hfl = hfl ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Checkbox("VFlip", &vfl)) actor->vfl = vfl ? 1 : 0;

    ImGui::Separator();
    ImGui::Text("BGND Code Hints");
    ImGui::Checkbox("Emit BGND code", &actor->emit_bgnd_code);
    runtime_actor_combo_string("Pattern", actor->code_pattern, sizeof actor->code_pattern,
                               k_runtime_actor_patterns,
                               (int)(sizeof k_runtime_actor_patterns / sizeof k_runtime_actor_patterns[0]));
    runtime_actor_combo_string("Frame Driver", actor->frame_driver, sizeof actor->frame_driver,
                               k_runtime_actor_frame_drivers,
                               (int)(sizeof k_runtime_actor_frame_drivers / sizeof k_runtime_actor_frame_drivers[0]));
    ImGui::InputText("Routine Label", actor->code_label, sizeof actor->code_label);
    ImGui::InputText("Insert List", actor->insert_list, sizeof actor->insert_list);
    ImGui::InputText("Scroll Symbol", actor->scroll_symbol, sizeof actor->scroll_symbol);
    ImGui::InputText("Sync Group", actor->sync_group, sizeof actor->sync_group);
    runtime_actor_combo_string("Group Role", actor->group_role, sizeof actor->group_role,
                               k_runtime_actor_group_roles,
                               (int)(sizeof k_runtime_actor_group_roles / sizeof k_runtime_actor_group_roles[0]));
    ImGui::InputInt("Group Index", &actor->group_index);
    ImGui::Checkbox("Sync Group Timing", &actor->sync_group_timing);
    ImGui::Checkbox("Multipart Piece", &actor->multipart_piece);
    ImGui::InputInt("Part Index", &actor->part_index);
    ImGui::InputInt("Part Count", &actor->part_count);
    if (actor->part_count < 1) actor->part_count = 1;
    if (actor->part_index < 0) actor->part_index = 0;
    if (actor->part_index >= actor->part_count) actor->part_index = actor->part_count - 1;
    ImGui::InputText("Parent Actor", actor->parent_actor, sizeof actor->parent_actor);
    ImGui::Separator();
    ImGui::Text("Group Tools");
    ImGui::InputInt("Group Nudge X", &g_runtime_actor_group_nudge_x);
    ImGui::InputInt("Group Nudge Y", &g_runtime_actor_group_nudge_y);
    if (ImGui::Button("Nudge Group", ImVec2(-1, 0)))
        runtime_actor_nudge_group(actor, g_runtime_actor_group_nudge_x, g_runtime_actor_group_nudge_y);
    if (ImGui::Button("Select Group Source Objects", ImVec2(-1, 0)))
        runtime_actor_select_group(actor);
    if (ImGui::Button("Copy Group BGND Skeleton", ImVec2(-1, 0)))
        runtime_actor_copy_group_bgnd_notes(actor);
    ImGui::TextDisabled("Group actors: %d", runtime_actor_group_count(actor));
    ImGui::InputInt("Delay Min Ticks", &actor->delay_min_ticks);
    ImGui::InputInt("Delay Max Ticks", &actor->delay_max_ticks);
    ImGui::InputInt("Motion X Vel", &actor->motion_x);
    ImGui::InputInt("Motion Y Vel", &actor->motion_y);
    if (ImGui::Button("Copy BGND Actor Skeleton", ImVec2(-1, 0)))
        runtime_actor_copy_bgnd_notes(actor);

    if (ImGui::Button("Append Active Image As Frame", ImVec2(-1, 0)))
        runtime_actor_append_active_frame(actor);
    if (ImGui::Button("Set Current Frame From Selected Object", ImVec2(-1, 0)))
        runtime_actor_set_current_frame_from_selected(actor);
    if (ImGui::Button("Replace Anchor From Selected Object", ImVec2(-1, 0)))
        runtime_actor_replace_anchor_from_selected(actor);
    if (ImGui::Button("Select Source Object", ImVec2(-1, 0)))
        runtime_actor_select_source_object(actor);
    if (ImGui::Button("Delete Actor", ImVec2(-1, 0))) {
        runtime_actor_remove_selected();
        return;
    }

    if (actor->frame_count < 1) actor->frame_count = 1;
    if (actor->frame_count > MAX_RUNTIME_ACTOR_FRAMES) actor->frame_count = MAX_RUNTIME_ACTOR_FRAMES;
    ImGui::InputInt("Frame Count", &actor->frame_count);
    if (actor->frame_count < 1) actor->frame_count = 1;
    if (actor->frame_count > MAX_RUNTIME_ACTOR_FRAMES) actor->frame_count = MAX_RUNTIME_ACTOR_FRAMES;

    ImGui::Separator();
    ImGui::Text("Frame Operations");
    if (ImGui::Button("Duplicate Current Frame", ImVec2(-1, 0)))
        runtime_actor_duplicate_current_frame(actor);
    if (ImGui::Button("Copy Current Frame Offsets", ImVec2(-1, 0)))
        runtime_actor_copy_current_frame_offsets(actor);
    if (ImGui::Button("Paste Current Frame Offsets", ImVec2(-1, 0)))
        runtime_actor_paste_current_frame_offsets(actor);
    if (ImGui::Button("Apply Current Offsets To All Frames", ImVec2(-1, 0)))
        runtime_actor_apply_current_offsets_to_all(actor);
    if (ImGui::Button("Clear All Frame Offsets", ImVec2(-1, 0)))
        runtime_actor_clear_frame_offsets(actor);

    if (ImGui::BeginTable("runtime_actor_frames", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("image label / index");
        ImGui::TableSetupColumn("dx", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("dy", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("ticks", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("hfl", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("vfl", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();
        for (int fi = 0; fi < actor->frame_count; fi++)
            draw_runtime_actor_frame_row(actor, fi);
        ImGui::EndTable();
    }

    int cur_frame = runtime_actor_current_frame(actor);
    int img_i = runtime_actor_frame_image_index(actor, cur_frame);
    if (img_i >= 0 && img_i < g_ni) {
        Img *im = &g_img[img_i];
        SDL_Texture *tex = editor_texture_at(img_i);
        if (tex) {
            float max_w = ImGui::GetContentRegionAvail().x;
            float sc = 2.0f;
            if (im->w * sc > max_w && im->w > 0) sc = max_w / (float)im->w;
            if (sc < 1.0f) sc = 1.0f;
            ImGui::Text("Current frame: %d / %d  dx=%d dy=%d ticks=%d", cur_frame + 1, actor->frame_count,
                        actor->frame_dx[cur_frame], actor->frame_dy[cur_frame],
                        runtime_actor_frame_ticks_at(actor, cur_frame));
            draw_editor_texture_transparent(tex, im->w * sc, im->h * sc);
        }
    }

    ImGui::Separator();
    runtime_actor_draw_validation(actor);
    if (g_runtime_actor_status[0]) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", g_runtime_actor_status);
    }
}
