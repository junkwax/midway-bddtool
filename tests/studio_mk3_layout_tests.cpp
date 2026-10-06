#include "Core/studio_mk3_layout.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
void write(const fs::path &p, const std::string &s) { std::ofstream(p) << s; }
std::string fixture() {
    return "test_mod\n\t.word 0\n\t.word >07\n\t.word >f0\n\t.word 400\n\t.word 0\n\t.word 1200-scrrgt\n"
        "\t.long calla\n\t.long rates\n\t.long lists\n\t.long bak1mods\n"
        "\t.long PLANE1BMOD\n\t.word ->10,25\n\t.long PLANE2BMOD\n\t.word 10,-8\n\t.long PLANE3BMOD\n\t.word 0,5\n"
        "\t.long center_x\n\t.long PLANE1BMOD,worldtlx1\n\t.long PLANE2BMOD,worldtlx2\n\t.long 0,0\n"
        "rates\n\t.long 0,0,0,0,0,>8000,>20000,>10000,>20000\n"
        "lists\n\t.long baklst3,worldtlx3+16\n\t.long baklst1,worldtlx1+16\n\t.long -1,floor_code\n\t.long baklst2,worldtlx2+16\n\t.long objlst,worldtlx+16\n\t.long 0\n";
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Scratch folder required.");
        auto scratch = fs::u8path(argv[1]) / std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        fs::create_directories(scratch); auto source = scratch / "MKBT.ASM"; write(source, fixture());
        auto doc = Document::demo(); auto before = doc.state(); std::string error;
        auto layout = read_mk3_layout(doc, source.u8string()); require(layout.valid(), layout.error);
        require(layout.start_x == 400 && layout.start_y == 7 && layout.ground == 247, "MK3 camera and ground fields swapped");
        require(doc.state().revision == before.revision && doc.state().assets == before.assets, "Review mutated the document");
        int minx[3] = {100000,100000,100000}, miny[3] = {100000,100000,100000}, maxx[3] = {};
        for (const auto &p : before.objects) {
            minx[p.plane] = std::min(minx[p.plane], p.object.depth); miny[p.plane] = std::min(miny[p.plane], p.object.sy);
            maxx[p.plane] = std::max(maxx[p.plane], p.object.depth + doc.image(p.object.ii)->w);
        }
        require(doc.apply_mk3_layout(layout, error), error);
        auto scene = doc.scene({400,7}), moved = doc.scene({480,10});
        const int ox[] = {-16,10,0}, oy[] = {25,-8,5}; const double rate[] = {.5,1,.25};
        for (size_t i = 0; i < scene.size(); ++i) {
            const auto &p = doc.state().objects[scene[i].object_index]; int n = p.plane;
            int origin = n < 2 ? (maxx[n] - minx[n]) / 2 - 199 : 400;
            require(scene[i].rect.x == p.object.depth - minx[n] + ox[n] - origin, "Independent layer centering failed");
            require(scene[i].rect.y == p.object.sy - miny[n] + oy[n] - 7, "Source-sheet Y was not normalized");
            require(moved[i].rect.x == scene[i].rect.x - 80 * rate[n] && moved[i].rect.y == scene[i].rect.y - 3, "Parallax projection failed");
        }
        require(doc.state().assets == before.assets && scene.front().rank == 0 && scene.back().rank == 3, "Pixels or display order changed");
        require(doc.undo() && doc.state().revision == before.revision && doc.state().planes[0].x == before.planes[0].x, "Layout undo failed");
        require(doc.redo(), "Layout redo failed");
        require(doc.save((scratch / "map.BDB").u8string(), error), error);
        Document reopened; require(reopened.load((scratch / "map.BDB").u8string(), error), error);
        require(reopened.has_layout() && reopened.state().start_y == 7 && reopened.state().ground == 247 && reopened.state().planes[0].scroll == .5, "Saved layout did not reopen");
        auto stale = read_mk3_layout(doc, source.u8string()); doc.move({doc.state().objects[0].id}, 1, 0);
        auto revision = doc.state().revision;
        require(!doc.apply_mk3_layout(stale, error) && doc.state().revision == revision, "Stale review was applied");
        auto bad = fixture(); auto pos = bad.find("1200-scrrgt"); bad.replace(pos, 11, "unknown_constant"); write(source, bad);
        require(!read_mk3_layout(doc, source.u8string()).valid(), "Unknown expression accepted");
        write(source, fixture() + fixture()); require(!read_mk3_layout(doc, source.u8string()).valid(), "Ambiguous labels accepted");
        write(source, fixture()); require(!read_mk3_layout(Document::empty(), source.u8string()).valid(), "Wrong source matched unrelated map");
        auto extended = fixture();
        auto at = extended.find("\t.long objlst,worldtlx+16");
        extended.insert(at, "\t.long baklst9,worldtlx1+16\n\t.long -1,use_next_y,worldtly\nshared_tail\n");
        extended += "variant_mod\n\t.long calla\n\t.long rates\n\t.long lists\n\t.long bak1mods\n\t.long PLANE1BMOD\n\t.word 0,0\n\t.long 0\n";
        write(source, extended);
        auto full = read_mk3_layout(Document::demo(), source.u8string());
        require(full.valid() && full.label == "TEST_MOD", "Actor list, shared tail, or complete-module matching failed: " + full.error);
        auto stationary = fixture(); at = stationary.find("0,0,0,0,0,>8000,>20000,>10000,>20000");
        stationary.replace(at, std::string("0,0,0,0,0,>8000,>20000,>10000,>20000").size(), "0,0,0,0,0,0,0,0,0");
        write(source, stationary);
        auto still = read_mk3_layout(Document::demo(), source.u8string());
        require(still.valid() && still.planes[0].scroll == 0, "Stationary UI-screen scroll table rejected");
        auto saved_scene = reopened.scene({400,7});
        require(saved_scene.size() == scene.size(), "Save changed scene count");
        for (size_t i = 0; i < scene.size(); ++i)
            require(saved_scene[i].rect.x == scene[i].rect.x && saved_scene[i].rect.y == scene[i].rect.y, "Save changed imported layer positions");
        if (argc >= 4) {
            int matched = 0, unavailable = 0;
            for (const auto &file : fs::directory_iterator(fs::u8path(argv[2]))) {
                auto ext = file.path().extension().u8string(); if (ext != ".BDB" && ext != ".bdb") continue;
                Document actual;
                if (!actual.load(file.path().u8string(), error)) { std::cout << file.path().filename().u8string() << ": load unavailable: " << error << '\n'; ++unavailable; continue; }
                auto plan = read_mk3_layout(actual, argv[3]);
                if (plan.valid()) {
                    require(actual.apply_mk3_layout(plan, error), error); ++matched;
                    std::cout << file.path().filename().u8string() << ": " << plan.label << " camera " << plan.start_x << ',' << plan.start_y << " (" << plan.planes.size() << " planes)\n";
                } else { ++unavailable; std::cout << file.path().filename().u8string() << ": unavailable: " << plan.error << '\n'; }
            }
            std::cout << "Local read-only scan: " << matched << " matched, " << unavailable << " unavailable. No local assets saved.\n";
            require(matched > 0, "No local MK3 maps matched");
        }
        std::cout << "MK3 layout camera fields, center_x, offsets, parallax, order, stale review, undo and save/reopen passed.\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
