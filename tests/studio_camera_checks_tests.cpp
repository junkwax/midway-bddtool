#include "Core/studio_camera_checks.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace studio;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
State fixture(int width = 200, int height = 254) {
    State s;
    auto bank = std::make_shared<AssetBank>();
    BddCoreImage im{}; im.idx = 1; im.w = width; im.h = height; im.pix.resize(size_t(width) * height, 1);
    bank->data.images.push_back(im);
    BddCorePalette pal{}; pal.count = 2;
    bank->data.palettes.assign(40, pal); bank->default_palettes = {0}; s.assets = bank;
    Plane plane; plane.bound = true; s.planes.push_back(plane);
    return s;
}
void place(State &s, int x, int y = 0, int pal = 0, int layer = 0) {
    Placement p; p.id = s.objects.size() + 1; p.plane = layer;
    p.object.ii = 1; p.object.depth = x; p.object.sy = y; p.object.fl = pal;
    s.objects.push_back(p);
}
bool finding(const CameraCheck &c, const char *text) {
    for (const auto &i : c.issues) if (i.message.find(text) != std::string::npos) return true;
    return false;
}
int main() {
    try {
        CameraCheckOptions o; o.reserve = 0;
        auto s = fixture(); place(s, 0); place(s, 200);
        auto original = s.assets;
        auto c = check_camera_range(s, o);
        require(c.complete && c.samples == 1 && c.peak_objects == 2 && c.peak_palettes == 1 && c.widest_gap == 0,
                "Tiled full-width bounds did not cover viewport");
        require(c.issues.empty() && s.assets == original && s.revision == 0, "Scan mutated source or flagged valid fixture");
        s.objects[1].object.depth = 220;
        c = check_camera_range(s, o);
        require(c.widest_gap == 20 && finding(c, "coverage gap") && c.issues.front().has_camera,
                "Interior gap missing or has no camera target");
        // Half-open viewport edges: touching without intersecting must not count.
        s.objects[0].object.depth = -200; s.objects[1].object.depth = 400;
        place(s, 0, 254); place(s, 0, -254);
        c = check_camera_range(s, o);
        require(c.peak_objects == 0 && c.widest_gap == 400, "Viewport edge contact counted as visible");
        // Final camera endpoint must be sampled even when the step does not divide the range.
        s = fixture(4, 4); place(s, 425, 10);
        o.max_x = 30; o.step = 16;
        c = check_camera_range(s, o);
        require(c.samples == 3 && c.peak_objects == 1 && c.object_camera.x == 30, "Final X endpoint omitted");
        o.min_x = o.max_x = 0; o.min_y = 0; o.max_y = 30;
        s.objects[0].object.depth = 10; s.objects[0].object.sy = 279;
        c = check_camera_range(s, o);
        require(c.samples == 3 && c.object_camera.y == 30, "Final Y endpoint omitted");
        // Layer transform, fractional/negative parallax and custom source shifts.
        s = fixture(4, 4); place(s, 420, 20); s.objects[0].runtime_dx = -10;
        s.planes[0].x = 15; s.planes[0].source.x1 = 5; s.planes[0].scroll = .5;
        o.min_y = o.max_y = 0; o.min_x = o.max_x = 50;
        c = check_camera_range(s, o); require(c.peak_objects == 1, "Parallax or source transform ignored");
        s.planes[0].scroll = -.5;
        c = check_camera_range(s, o); require(c.peak_objects == 0, "Negative parallax ignored");
        // Hidden art still contributes to saved runtime costs; palettes are unique per sample.
        s = fixture(4, 4);
        for (int i = 0; i < 303; ++i) { place(s, 0, 0, i % 36); s.objects.back().hidden = true; }
        s.planes[0].hidden = true; o = {}; // Reserve defaults to 56.
        c = check_camera_range(s, o);
        require(c.peak_objects == 303 && c.peak_palettes == 36 && finding(c, "object pressure") && finding(c, "palette pressure"),
                "Hidden placements, reserve or palette deduplication incorrect");
        o.reserve = 0; c = check_camera_range(s, o);
        require(!finding(c, "object pressure") && finding(c, "headroom"), "Reserve override ignored");
        require(camera_check_current(c, s, o), "Fresh result marked stale");
        auto changed = s; ++changed.revision;
        require(!camera_check_current(c, changed, o), "Edited result remained current");
        changed = s; changed.assets = std::make_shared<AssetBank>(*s.assets);
        require(!camera_check_current(c, changed, o), "Changed bank remained current");
        auto other = o; ++other.step;
        require(!camera_check_current(c, s, other), "Changed options remained current");
        require(camera_check_report(c).find("Samples may miss") != std::string::npos, "Report hides sampling limits");
        // Incomplete references/unbound projections must not be reported as a runtime proof.
        s = fixture(); place(s, 0); place(s, 200); s.planes[0].bound = false;
        s.objects[1].object.ii = 999;
        c = check_camera_range(s, o);
        require(c.unresolved == 2 && c.peak_objects == 1 && finding(c, "unresolved runtime"), "Missing/unbound sources silently treated as verified");
        s.planes[0].scroll = std::nan(""); c = check_camera_range(s, o);
        require(c.peak_objects == 0 && c.unresolved == 2, "Invalid parallax reached projection");
        o.min_x = 10; o.max_x = 0; require(!check_camera_range(s, o).complete, "Reversed range accepted");
        o = {}; o.step = 0; require(!check_camera_range(s, o).complete, "Zero step accepted");
        o = {}; o.max_x = o.max_y = 100000; require(!check_camera_range(s, o).complete, "Unbounded sample workload accepted");
        o = {}; o.reserve = -1; require(!check_camera_range(s, o).complete, "Negative reserve accepted");
        o = {}; OptimizeProgress progress; progress.cancel = true;
        c = check_camera_range(s, o, &progress);
        require(c.cancelled && !c.complete && c.issues.empty(), "Cancelled scan returned current findings");
        s.has_bdb = false; require(!check_camera_range(s, o).complete, "Standalone BDD scanned as a stage");
        std::cout << "Camera/resource checks passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
