#include "Core/studio_runtime_actors.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace studio;
namespace fs = std::filesystem;
void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }

// Shaped like the older NUPOOL sidecar: optional keys are absent and must take legacy defaults.
const char *kFixture = R"({
  "version": 3, "source_type": "bddview_runtime_actors", "stage": "FIXTURE",
  "actors": [
    {"name": "body", "trigger": "always", "sync_group": "pair", "group_role": "leader", "group_index": 0,
     "x": 92, "y": 14, "scroll": 1.0, "frame_ticks": 6, "loop": true, "frames": ["A1", "A2", "A3"]},
    {"name": "skeleton", "trigger": "drop_0x0240", "sync_group": "pair", "group_role": "follower",
     "group_index": 0, "sync_group_timing": true, "x": 92, "y": 192, "frame_ticks": 4, "delay_min_ticks": 576,
     "delay_max_ticks": 576, "loop": false, "frames": ["B1", "B2"], "frame_ticks_override": [0, 10],
     "frame_dx": [0, 3], "frame_hfl_mode": [0, 2]},
    {"name": "eric \"static\"", "trigger": "always_static", "x": 470, "y": 10, "phase_ticks": 7,
     "frames": ["C1", "C2"], "motion_x": 65536}
  ]
})";

int main(int argc, char **argv) {
    try {
        RuntimeActorSet set; std::string error;
        require(parse_runtime_actors(kFixture, set, error), error);
        require(set.stage == "FIXTURE" && set.actors.size() == 3, "Fixture header or count is wrong");
        const auto &body = set.actors[0];
        require(body.screen_space_y && body.emit_bgnd_code && body.enabled && body.code_label == "body" &&
                body.insert_list == "baklst4" && body.frame_driver == "frame_a9", "Legacy defaults were not applied");
        require(set.actors[1].frames[1].ticks == 10 && set.actors[1].frames[1].dx == 3 && set.actors[1].frames[1].hfl_mode == 2,
                "Per-frame arrays were not read");
        require(set.actors[2].code_label == "eric__static_", "Code label was not sanitized like the legacy tool");

        // Written output parses back identically and is stable on a second write.
        auto text = write_runtime_actors(set);
        RuntimeActorSet again; require(parse_runtime_actors(text, again, error), error);
        require(write_runtime_actors(again) == text, "Sidecar write is not stable across a round trip");
        require(text.find("\"scroll\": 1.0000") != std::string::npos && text.find("\\\"static\\\"") != std::string::npos,
                "Sidecar formatting differs from the legacy writer");

        // Legacy timing: frames advance by frame_ticks and loop.
        require(actor_pose(set, 0, 0, false).frame == 0 && actor_pose(set, 0, 6, false).frame == 1 &&
                actor_pose(set, 0, 18, false).frame == 0, "Looping frame timing is wrong");
        // Without trigger simulation the delayed follower plays immediately (legacy preview).
        auto follower = actor_pose(set, 1, 4, false);
        require(follower.visible && follower.frame == 1 && follower.hflip && follower.x == 95, "Legacy follower pose is wrong");
        require(actor_pose(set, 1, 1000, false).frame == 1, "Non-looping actor did not hold its last frame");
        // With simulation it waits for its delay, then starts from frame zero.
        require(!actor_pose(set, 1, 575, true).visible, "Delayed actor appeared early");
        auto dropped = actor_pose(set, 1, 576, true);
        require(dropped.visible && dropped.frame == 0, "Delayed actor did not start at its delay");
        // always_static holds its first frame only when simulating; phase and motion still apply.
        require(actor_pose(set, 2, 0, false).frame == 1 && actor_pose(set, 2, 0, true).frame == 0,
                "always_static or phase handling is wrong");
        require(actor_pose(set, 2, 3, false).x == 470 + 10, "Motion offset ignores phase");
        set.actors[0].enabled = false;
        require(!actor_pose(set, 0, 0, false).visible, "Disabled actor is visible");

        auto skeleton = actor_bgnd_skeleton(set.actors[1]);
        require(skeleton.find("skeleton_proc") != std::string::npos && skeleton.find("\t.long\t0\n") != std::string::npos &&
                skeleton.find("0" "00c0005ch") != std::string::npos, "BGND skeleton is wrong");
        require(!parse_runtime_actors("{\"actors\": [1]}", set, error) && !parse_runtime_actors("{\"actors\": [", set, error),
                "Malformed sidecars were accepted");

        // Optional: the real checkout's sidecars and art, e.g. studio_runtime_actors_tests C:/mk2/data
        if (argc >= 2) {
            auto start = std::chrono::steady_clock::now();
            auto index = index_runtime_art(argv[1]);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            std::cout << "Indexed " << index.files.size() << " labels in " << index.scanned << " IMG files ("
                      << index.unreadable << " unreadable) in " << ms << " ms\n";
            for (const char *stage : {"NUPOOL", "FOREST2"}) {
                auto path = fs::u8path(argv[1]) / (std::string(stage) + ".runtime.json");
                if (!fs::exists(path)) continue;
                std::ifstream in(path, std::ios::binary); std::stringstream buf; buf << in.rdbuf();
                RuntimeActorSet real; require(parse_runtime_actors(buf.str(), real, error), std::string(stage) + ": " + error);
                auto art = load_runtime_art(index, real);
                std::cout << stage << ": " << real.actors.size() << " actors, " << art.frames.size() << " frames resolved, "
                          << art.missing.size() << " missing, " << art.errors.size() << " errors\n";
                for (const auto &m : art.missing) std::cout << "  missing " << m << "\n";
                for (const auto &e : art.errors) std::cout << "  error " << e << "\n";
                // Report how the anchor-free sidecar placement compares with the assembly-driven preview.
                Document document;
                auto bdb = (fs::u8path(argv[1]) / (std::string(stage) + ".BDB")).u8string();
                if (!document.load(bdb, error)) continue;
                auto built_in = load_animation_preview(document, fs::u8path(argv[1]).parent_path().u8string());
                for (size_t i = 0; built_in.ready() && i < built_in.anchors.size() && i < real.actors.size(); ++i) {
                    auto r = built_in.rect(i, 0, {});
                    auto pose = actor_pose(real, i, 0, false);
                    std::cout << "  " << real.actors[i].name << ": sidecar " << pose.x << "," << pose.y
                              << " vs assembly preview " << r.x << "," << r.y << "\n";
                }
            }
        }
        std::cout << "Sidecar defaults, stable round trip, legacy timing, trigger simulation and skeleton passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
