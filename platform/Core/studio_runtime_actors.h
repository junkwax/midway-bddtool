#pragma once

#include "Core/studio_animation.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace studio {

// Editor-side runtime actor sidecars (<STAGE>.runtime.json, legacy bddview format v3).
// The game never reads them: actors run from hand-written assembly. These records
// drive the authoring preview and the copied BGND skeleton only.
struct RuntimeActorFrame {
    std::string label;          // IMG image label, or a numeric image index.
    int dx = 0, dy = 0;
    int ticks = 0;              // 0 uses the actor's frame_ticks.
    int hfl_mode = 0, vfl_mode = 0; // 0 inherit, 1 off, 2 on.
};
struct RuntimeActor {
    std::string name, trigger = "always", code_pattern = "loop_sprite", code_label;
    std::string insert_list = "baklst4", scroll_symbol = "worldtlx4", sync_group;
    std::string frame_driver = "frame_a9", group_role = "solo", parent_actor;
    std::vector<RuntimeActorFrame> frames;
    int source_object = -1, x = 0, y = 0, layer = 0;
    double scroll = 1;
    int frame_ticks = 6, phase_ticks = 0, delay_min_ticks = 0, delay_max_ticks = 0;
    int motion_x = 0, motion_y = 0; // 16.16 pixels per tick.
    int group_index = 0, part_index = 0, part_count = 1;
    bool hfl = false, vfl = false, enabled = true, loop = true, replace_source = false;
    bool emit_bgnd_code = false, sync_group_timing = false, multipart_piece = false;
    bool screen_space_y = true; // Y stays fixed on screen instead of following the camera.
};
struct RuntimeActorSet {
    std::string stage;
    std::vector<RuntimeActor> actors;
};
constexpr int kMaxRuntimeActors = 64, kMaxRuntimeFrames = 24;
constexpr double kRuntimeTicksPerSecond = 53.0; // The legacy preview's rate.

// <directory>/<STEM>.runtime.json beside the stage's BDB/BDD.
std::string runtime_actor_sidecar_path(const std::string &document_path);
bool parse_runtime_actors(const std::string &json, RuntimeActorSet &out, std::string &error);
std::string write_runtime_actors(const RuntimeActorSet &set);
std::string make_actor_code_label(const std::string &name);

int actor_frame_ticks(const RuntimeActor &actor, size_t frame);
int actor_cycle_ticks(const RuntimeActor &actor);
struct ActorPose {
    bool visible = false;
    size_t frame = 0;
    int x = 0, y = 0; // Stage position before camera projection, motion included.
    bool hflip = false, vflip = false;
};
// Legacy timing: every enabled actor plays from tick zero plus its phase (or its sync leader's).
// With simulate_triggers, delayed actors appear after delay_min_ticks and always_static holds
// its first frame; the game's own randomness and drop logic are not reproduced.
ActorPose actor_pose(const RuntimeActorSet &set, size_t index, int64_t ticks, bool simulate_triggers);
// Earliest preview tick displaying a requested frame, or -1 when current
// trigger/phase rules make that frame unreachable.
int64_t actor_tick_for_frame(const RuntimeActorSet &set, size_t index, size_t frame,
                             bool simulate_triggers);
// BDB object indexes suppressed by visible actors explicitly marked as replacements.
// Invalid source indexes and currently hidden actors are ignored.
std::vector<size_t> actor_replaced_sources(const RuntimeActorSet &set, int64_t ticks,
                                           bool simulate_triggers, size_t object_count);

// Copies the legacy hand-integration skeleton; it is not a verified game driver.
std::string actor_bgnd_skeleton(const RuntimeActor &actor);

// Label -> IMG file index over the stage directory; reads IMG headers only.
struct RuntimeArtIndex {
    std::map<std::string, std::string> files; // Upper-case label -> path.
    size_t scanned = 0, unreadable = 0;
};
RuntimeArtIndex index_runtime_art(const std::string &directory);
struct RuntimeArt {
    std::vector<AnimationPreview> sources;
    std::map<std::string, std::pair<size_t, size_t>> frames; // Label -> source, frame.
    std::vector<std::string> missing, errors;
    const AnimationFrame *find(const std::string &label, size_t *source = nullptr) const;
};
RuntimeArt load_runtime_art(const RuntimeArtIndex &index, const RuntimeActorSet &set);

} // namespace studio
