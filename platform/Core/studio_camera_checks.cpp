#include "Core/studio_camera_checks.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace studio {
bool CameraCheckOptions::operator==(const CameraCheckOptions &o) const {
    return std::tie(min_x, max_x, min_y, max_y, step, reserve, all_raw) ==
           std::tie(o.min_x, o.max_x, o.min_y, o.max_y, o.step, o.reserve, o.all_raw);
}
bool camera_check_current(const CameraCheck &c, const State &s, const CameraCheckOptions &o) {
    return c.complete && c.before.assets == s.assets && c.before.revision == s.revision && c.options == o;
}
CameraCheck check_camera_range(const State &state, const CameraCheckOptions &o, OptimizeProgress *progress) {
    CameraCheck result; result.before = state; result.options = o;
    if (!state.assets || !state.has_bdb) {
        result.error = "Open a stage with a BDB layout before scanning cameras."; return result;
    }
    if (o.min_x > o.max_x || o.min_y > o.max_y || o.min_x < -100000 || o.max_x > 100000 ||
        o.min_y < -100000 || o.max_y > 100000 || o.step < 1 || o.step > 100000 ||
        o.reserve < 0 || o.reserve > BDD_CORE_MK2_DISPLAY_OBJECT_CAP) {
        result.error = "Use ordered camera bounds within -100000..100000, a positive step up to 100000, and a reserve of 0..358.";
        return result;
    }
    auto axis = [&](int lo, int hi) {
        std::vector<int> points;
        for (int64_t v = lo; v < hi; v += o.step) points.push_back((int)v);
        points.push_back(hi); return points;
    };
    auto xs = axis(o.min_x, o.max_x), ys = axis(o.min_y, o.max_y);
    if (xs.size() * ys.size() > 2048 || state.objects.size() > BDD_CORE_MAX_OBJECTS) {
        result.error = "Scan limit: 2048 camera samples and 8192 placements. Increase the step or narrow the range.";
        return result;
    }
    if (progress) { progress->done = 0; progress->total = (int)(xs.size() * ys.size()); }
    struct Block { double x, y, w, h, scroll; int palette, image; };
    std::vector<Block> blocks;
    std::map<int, const BddCoreImage *> images;
    std::set<int> duplicate_ids;
    for (const auto &im : state.assets->data.images)
        if (!images.emplace(im.idx, &im).second) duplicate_ids.insert(im.idx);
    for (const auto &p : state.objects) {
        auto it = images.find(p.object.ii);
        if (it == images.end() || duplicate_ids.count(p.object.ii) || p.plane < 0 || p.plane >= (int)state.planes.size()) {
            ++result.unresolved; continue;
        }
        const auto &im = *it->second;
        const auto &plane = state.planes[p.plane];
        if (im.w <= 0 || im.h <= 0 || im.w > 4096 || im.h > 4096 || !std::isfinite(plane.scroll)) {
            ++result.unresolved; continue;
        }
        bool palette_valid = p.object.fl >= 0 && p.object.fl < (int)state.assets->data.palettes.size();
        if (!plane.bound || !palette_valid) ++result.unresolved;
        blocks.push_back({double(p.object.depth) + p.runtime_dx + double(plane.x) - plane.source.x1,
                          double(p.object.sy) + double(plane.y) - plane.source.y1,
                          double(im.w), double(im.h), plane.scroll, palette_valid ? p.object.fl : -1, im.idx});
    }
    result.object_camera = result.palette_camera = result.gap_camera = {double(o.min_x), double(o.min_y)};
    for (int cy : ys) for (int cx : xs) {
        if (progress && progress->cancel) { result.cancelled = true; return result; }
        int objects = 0;
        std::set<int> palettes;
        std::vector<std::pair<double, double>> spans;
        std::vector<const Block *> visible;
        double cost_us[2] = {};
        for (const auto &b : blocks) {
            double x = b.x - cx * b.scroll, y = b.y - cy;
            if (x >= 400 || x + b.w <= 0 || y >= 254 || y + b.h <= 0) continue;
            ++objects;
            if (b.palette >= 0) palettes.insert(b.palette);
            spans.emplace_back(std::max(0.0, x), std::min(400.0, x + b.w));
            // MKDISP culls off-screen blocks and queues the original size for
            // blocks touching the viewport, including ones cut by its edge.
            visible.push_back(&b);
            for (int board = 0; board < 2; ++board)
                cost_us[board] += stage_block_cost_us(kStageBudgetBoards[board], b.w, b.h, o.all_raw);
        }
        Point camera{double(cx), double(cy)};
        bool ranked = false;
        for (int board = 0; board < 2; ++board) {
            auto &peak = result.budget[board];
            double ms = cost_us[board] / 1000;
            if (ms <= peak.ms && result.samples) continue;
            peak.ms = ms;
            peak.camera = camera;
            peak.largest.clear();
            const size_t count = std::min<size_t>(5, visible.size());
            if (!ranked) {
                std::partial_sort(visible.begin(), visible.begin() + count, visible.end(),
                    [](const Block *a, const Block *b) { return a->w * a->h > b->w * b->h; });
                ranked = true;
            }
            for (size_t i = 0; i < count; ++i) {
                const auto &b = *visible[i];
                peak.largest.push_back({b.image, int(b.w), int(b.h),
                                        stage_block_cost_us(kStageBudgetBoards[board], b.w, b.h, o.all_raw) / 1000});
            }
        }
        if (objects > result.peak_objects) { result.peak_objects = objects; result.object_camera = camera; }
        if ((int)palettes.size() > result.peak_palettes) { result.peak_palettes = (int)palettes.size(); result.palette_camera = camera; }
        std::sort(spans.begin(), spans.end());
        double right = 0, gap = 0;
        for (const auto &span : spans) { gap = std::max(gap, span.first - right); right = std::max(right, span.second); }
        gap = std::max(gap, 400 - right);
        if (gap > result.widest_gap) { result.widest_gap = gap; result.gap_camera = camera; }
        ++result.samples;
        if (progress) progress->done = result.samples;
    }
    auto finding = [&](std::string message, std::string next, Point camera, bool jump = true) {
        Issue issue; issue.group = IssueGroup::Camera; issue.message = std::move(message);
        issue.next_step = std::move(next); issue.has_camera = jump; issue.camera = camera;
        result.issues.push_back(std::move(issue));
    };
    auto ms_text = [](double ms) { char b[32]; snprintf(b, sizeof b, "%.2f", ms); return std::string(b); };
    for (int board = 0; board < 2; ++board) {
        const auto &peak = result.budget[board];
        if (peak.ms <= kStageArtWarnMs) continue;
        bool over = peak.ms > kStageArtOverMs;
        std::string dearest;
        for (const auto &b : peak.largest) {
            if (!dearest.empty()) dearest += ", ";
            dearest += "image " + std::to_string(b.image) + " " + std::to_string(b.width) + "x" + std::to_string(b.height) +
                       " (" + ms_text(b.ms) + " ms)";
        }
        finding(std::string(kStageBudgetBoards[board].name) + " stage art " + (over ? "over budget: " : "near budget: ") +
                    ms_text(peak.ms) + " ms a frame (target " + ms_text(kStageArtWarnMs) + ", late frames from about " +
                    ms_text(kStageArtOverMs) + ")",
                "Crop sparse blocks and check the cost of extra blits before splitting. Most expensive blocks: " + dearest + ".",
                peak.camera);
        result.issues.back().error = over;
    }
    if (result.peak_objects > BDD_CORE_MK2_DMA_QUEUE_CAP) {
        finding("Sampled DMA queue demand: " + std::to_string(result.peak_objects) +
                " static blits alone exceed the " + std::to_string(BDD_CORE_MK2_DMA_QUEUE_CAP) + "-entry queue",
                "Reduce simultaneous stage blits. The fighters, floor, shadows and HUD also use the queue.", result.object_camera);
        result.issues.back().error = true;
    }
    if (result.peak_objects + o.reserve > BDD_CORE_MK2_DISPLAY_OBJECT_CAP) {
        finding("Sampled object pressure: " + std::to_string(result.peak_objects) + " background + " +
                std::to_string(o.reserve) + " reserved exceeds the 358-object pool",
                "Review subdivision/reuse costs in Optimize and test this camera in game. The reserve is an estimate for actors, fighters and effects.", result.object_camera);
        result.issues.back().error = result.peak_objects > BDD_CORE_MK2_DISPLAY_OBJECT_CAP;
    }
    else if (result.peak_objects + o.reserve >= BDD_CORE_MK2_DISPLAY_OBJECT_WARN)
        finding("Sampled object headroom is low: " + std::to_string(BDD_CORE_MK2_DISPLAY_OBJECT_CAP - result.peak_objects - o.reserve) + " slots after reserve",
                "Check this camera with stage animations, fighters and effects active before adding more pieces.", result.object_camera);
    if (result.peak_palettes > BDD_CORE_MK2_BG_DYNAMIC_PALETTE_SLOTS)
        finding("Sampled palette pressure: " + std::to_string(result.peak_palettes) + " visible palettes exceeds 35 background slots",
                "Review palette sharing without automatic remapping. Allocation can persist across camera positions; animation palettes are not counted.", result.palette_camera);
    if (result.widest_gap > .01)
        finding("Sampled horizontal coverage gap: " + std::to_string((int)std::ceil(result.widest_gap)) + " px with no static artwork bounds",
                "Inspect this camera for unintended blank columns. Intentional empty space and runtime overlays may explain the gap; transparency and vertical holes are not tested.", result.gap_camera);
    if (result.unresolved || !state.runtime_profile.empty())
        finding("Camera estimates have unresolved runtime mapping" + (result.unresolved ? ": " + std::to_string(result.unresolved) + " placements" : std::string()),
                "Missing references are omitted and unbound layers use authored transforms. Custom runtime generation, dynamic motion and animation require in-game checks.", {}, false);
    result.complete = true;
    return result;
}
std::string camera_check_report(const CameraCheck &c) {
    std::ostringstream out;
    out << "Camera scan: X " << c.options.min_x << ".." << c.options.max_x << ", Y " << c.options.min_y << ".." << c.options.max_y
        << ", step " << c.options.step << ", reserve " << c.options.reserve << "\n";
    if (!c.complete) { out << (c.cancelled ? "Cancelled" : c.error) << "\n"; return out.str(); }
    out << c.samples << " samples (range endpoints included), 400x254 viewport.\n"
        << "Peak static objects: " << c.peak_objects << " at " << c.object_camera.x << "," << c.object_camera.y << "\n"
        << "Peak visible palettes: " << c.peak_palettes << " at " << c.palette_camera.x << "," << c.palette_camera.y << "\n"
        << "Widest uncovered horizontal span: " << c.widest_gap << " at " << c.gap_camera.x << "," << c.gap_camera.y << "\n"
        << "Stage art per frame (target " << kStageArtWarnMs << " ms, late frames from about " << kStageArtOverMs << " ms): "
        << kStageBudgetBoards[0].name << " " << c.budget[0].ms << " ms at " << c.budget[0].camera.x << "," << c.budget[0].camera.y << "; "
        << kStageBudgetBoards[1].name << " " << c.budget[1].ms << " ms at " << c.budget[1].camera.x << "," << c.budget[1].camera.y << "\n"
        << "Timing uses full block rectangles and "
        << (c.options.all_raw ? "the raw rate (all blocks assumed raw)." : "the conservative zero-compressed rate.") << "\n"
        << "Includes editor-hidden placements. Bounds only; no pixel/occlusion proof. Samples may miss peaks between positions.\n"
        << "Actors, animation, allocation lifetime and packed output require separate verification.\n";
    return out.str();
}
} // namespace studio
