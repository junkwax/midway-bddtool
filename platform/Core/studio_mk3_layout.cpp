#include "Core/studio_mk3_layout.h"
#include "Core/studio_assembly.h"
#include <cmath>
#include <cctype>
#include <regex>
#include <sstream>

namespace studio {
namespace {
using namespace assembly;
struct Expression {
    std::string text; size_t pos = 0;
    void space() { while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos; }
    bool take(char c) { space(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
    int64_t atom() {
        if (take('+')) return atom();
        if (take('-')) return -atom();
        if (take('(')) { auto n = sum(); require(take(')'), "Unclosed layout expression."); return n; }
        space(); size_t begin = pos;
        if (pos < text.size() && text[pos] == '>') ++pos;
        while (pos < text.size() && (std::isalnum((unsigned char)text[pos]) || text[pos] == '_')) ++pos;
        auto token = text.substr(begin, pos - begin);
        if (token == "SCRRGT") return 399;
        if (token == "CENTER_X") return -2;
        if (token == "NULL_IRQSKYE") return -1;
        require(!token.empty(), "Missing layout expression operand.");
        return number(token);
    }
    static int64_t bounded(int64_t v) { require(v >= -0xffffffffLL && v <= 0xffffffffLL, "Layout expression overflow."); return v; }
    int64_t product() {
        auto n = atom();
        while (true) {
            if (take('*')) {
                auto rhs = atom();
                require(!rhs || std::abs(n) <= 0xffffffffLL / std::abs(rhs), "Layout expression overflow.");
                n = bounded(n * rhs);
            } else if (take('/')) { auto d = atom(); require(d != 0, "Division by zero in layout."); n /= d; }
            else return n;
        }
    }
    int64_t sum() { auto n = product(); while (true) { if (take('+')) n = bounded(n + product()); else if (take('-')) n = bounded(n - product()); else return n; } }
    int value() { auto n = sum(); space(); require(pos == text.size() && n >= -1000000 && n <= 1000000, "Unsupported layout expression: " + text); return (int)n; }
};
int eval(const std::string &s) { require(s.size() <= 128, "Layout expression too long."); return Expression{s}.value(); }
std::vector<std::string> csv(const std::string &s) {
    std::vector<std::string> out; std::istringstream in(s); std::string part;
    while (std::getline(in, part, ',')) out.push_back(trim(part));
    return out;
}
bool directive(const std::string &line, const std::string &op, std::vector<std::string> &args) {
    if (line.rfind(op, 0) != 0 || line.size() <= op.size() || !std::isspace((unsigned char)line[op.size()])) return false;
    args = csv(trim(line.substr(op.size()))); return true;
}
struct Entry { int slot = 0, x = 0, y = 0; std::string module; };
struct Bounds { int x = 0, y = 0, width = 0; };
}

Mk3Layout read_mk3_layout(const Document &doc, const std::string &path) {
    Mk3Layout out; out.revision = doc.state().revision; out.assets = doc.state().assets; out.source = path;
    try {
        require(doc.state().runtime_profile.empty(), "This custom stage uses its own runtime layout.");
        auto input = fs::u8path(path);
        require(upper(input.filename().u8string()) == "MKBT.ASM", "Choose MK3's MKBT.ASM stage-definition file.");
        require(fs::file_size(input) <= 8 * 1024 * 1024, "Stage-definition file is too large.");
        Assembly source(input);
        std::map<std::string, const Plane *> planes;
        for (const auto &p : doc.state().planes) require(planes.emplace(upper(p.source.name), &p).second, "Repeated source module name.");
        std::vector<std::string> selected; int best_match = 0;
        for (const auto &label : source.labels) {
            if (label.first.size() <= 4 || label.first.substr(label.first.size() - 4) != "_MOD") continue;
            bool matching = true; int modules = 0; std::vector<std::string> args;
            for (const auto &line : source.block(label.first)) if (directive(line, ".LONG", args)) {
                if (args[0] == "CENTER_X") break;
                if (args[0].size() > 4 && args[0].substr(args[0].size() - 4) == "BMOD") {
                    matching &= planes.count(args[0].substr(0, args[0].size() - 4)) != 0; ++modules;
                }
            }
            if (matching && modules >= best_match && modules) {
                if (modules > best_match) { selected.clear(); best_match = modules; }
                selected.push_back(label.first);
            }
        }
        require(!selected.empty(), "No MK3 stage definition matches this map's module names.");
        require(selected.size() == 1, "More than one MK3 stage definition matches; choose an unambiguous source revision.");
        out.label = selected.front();
        std::vector<int> header;
        std::vector<std::string> longs, args;
        std::vector<Entry> entries;
        std::map<int, std::string> centers;
        bool centering = false, terminated = false; int slot = 0, pending = -1;
        auto stage_start = source.at(out.label);
        for (size_t line_index = stage_start; line_index < source.end(stage_start); ++line_index) {
            const auto &line = source.lines[line_index];
            if (line.empty()) continue;
            if (directive(line, ".WORD", args)) {
                if (longs.empty()) for (const auto &arg : args) header.push_back(eval(arg));
                else {
                    require(pending >= 0 && args.size() == 2, "Unsupported MK3 module offsets.");
                    entries[pending].x = eval(args[0]); entries[pending].y = eval(args[1]); pending = -1;
                }
            } else if (directive(line, ".LONG", args)) {
                require(pending < 0, "Module offsets are missing.");
                if (longs.size() < 4) { require(args.size() == 1, "Unsupported stage header."); longs.push_back(args[0]); continue; }
                if (args[0] == "0" || args[0] == ">FFFFFFFF" || args[0] == "0FFFFFFFFH" || args[0] == "-1") {
                    terminated = true; out.floor_line = line_index + 1; break;
                }
                if (args[0] == "CENTER_X") { require(!centering && args.size() == 1, "Repeated center_x directive."); centering = true; continue; }
                if (centering) {
                    std::smatch m;
                    require(args.size() == 2 && std::regex_match(args[1], m, std::regex(R"(WORLDTLX([1-8]?))")), "Unsupported center_x target.");
                    int target = m[1].str().empty() ? 0 : std::stoi(m[1]);
                    require(centers.emplace(target, args[0]).second, "Repeated center_x target.");
                } else {
                    require(++slot <= 8 && args.size() == 1, "Unsupported background slots.");
                    if (args[0] == "SKIP_BAKMOD") continue;
                    require(args[0].size() > 4 && args[0].substr(args[0].size() - 4) == "BMOD", "Unsupported background module.");
                    entries.push_back({slot, 0, 0, args[0].substr(0, args[0].size() - 4)}); pending = (int)entries.size() - 1;
                }
            } else require(false, "Unsupported stage directive: " + line);
        }
        require(terminated && header.size() == 6 && longs.size() == 4 && longs[3] == "BAK1MODS" && !entries.empty(), "Incomplete MK3 stage definition.");
        std::map<std::string, Bounds> bounds;
        for (const auto &entry : entries) {
            require(planes.count(entry.module) && !bounds.count(entry.module), "Missing or repeated runtime module.");
            Bounds b; int right = 0; bool found = false;
            for (const auto &p : doc.state().objects) {
                if (p.plane < 0 || p.plane >= (int)doc.state().planes.size() || upper(doc.state().planes[p.plane].source.name) != entry.module) continue;
                auto im = doc.image(p.object.ii); require(im != nullptr, "Module has a missing image.");
                if (!found) { b.x = p.object.depth; b.y = p.object.sy; right = b.x + im->w; found = true; }
                else { b.x = std::min(b.x, p.object.depth); b.y = std::min(b.y, p.object.sy); right = std::max(right, p.object.depth + im->w); }
            }
            require(found, "No assigned artwork in module " + entry.module + ". Resolve ownership before importing its layout.");
            b.width = right - b.x; bounds[entry.module] = b;
        }
        auto centered = [&](const std::string &symbol) {
            require(symbol.size() > 4 && symbol.substr(symbol.size() - 4) == "BMOD", "Unsupported centered module.");
            auto name = symbol.substr(0, symbol.size() - 4);
            require(bounds.count(name), "Centering references a module absent from this map.");
            return bounds.at(name).width / 2 - 399 / 2;
        };
        out.start_y = header[1]; out.ground = header[1] + header[2]; out.start_x = header[3];
        if (centers.count(0)) out.start_x = centered(centers.at(0));
        require(out.start_x != -2, "center_x camera has no explicit worldtlx initializer.");
        require(header[4] <= header[5], "Invalid scroll limits.");
        std::vector<int> scroll;
        for (const auto &line : source.block(longs[1])) {
            if (line.empty()) continue;
            require(directive(line, ".LONG", args), "Unsupported MK3 scroll table.");
            for (const auto &arg : args) scroll.push_back(eval(arg));
        }
        require(scroll.size() == 9 && (scroll.back() == 0x20000 ||
                std::all_of(scroll.begin(), scroll.end(), [](int n) { return n == 0; })), "Unsupported MK3 player scroll scale.");
        out.display_list = longs[2];
        for (int value : scroll) out.scroll_rates.push_back(value / 131072.0);
        std::map<int, int> ranks, projections; bool ended = false; int rank = 0;
        auto list_start = source.at(longs[2]);
        // Display-list tails can have another label and fall through (dlists_bogus).
        for (size_t i = list_start; i < source.lines.size() && i < list_start + 128; ++i) {
            const auto &line = source.lines[i];
            if (line.empty()) continue;
            require(directive(line, ".LONG", args), "Unsupported MK3 display list.");
            if (args[0] == "0") { ended = true; break; }
            std::smatch m;
            if (std::regex_match(args[0], m, std::regex(R"(BAKLST([1-9]))"))) {
                int n = std::stoi(m[1]);
                require(args.size() == 2 && std::regex_match(args[1], m, std::regex(R"(WORLDTLX([1-8]?)\+16)")), "Unsupported background camera projection.");
                projections[n] = m[1].str().empty() ? 0 : std::stoi(m[1]);
                require(ranks.emplace(n, rank).second, "Repeated background display-list slot.");
            } else if (args.size() == 3 && args[0] == "-1" && args[1] == "USE_NEXT_Y")
                require(args[2] == "WORLDTLY", "Separate runtime Y coordinates are unsupported.");
            else require(args.size() == 2 && (args[0] == "-1" || args[0] == "OBJLST" || args[0] == "OBJLST2"), "Unsupported display-list callback.");
            ++rank;
        }
        require(ended, "Unterminated display list.");
        std::ostringstream report;
        report << out.label << " | camera " << out.start_x << ", " << out.start_y << " | ground " << out.ground << "\n";
        for (const auto &entry : entries) {
            require(ranks.count(entry.slot), "A background module is absent from the display list.");
            Plane p = *planes.at(entry.module); const auto &b = bounds.at(entry.module);
            p.scroll = scroll[8 - entry.slot] / 131072.0;
            require(std::abs(p.scroll) <= 16, "Unsupported parallax rate.");
            int origin = centers.count(entry.slot) ? centered(centers.at(entry.slot)) : out.start_x;
            int projected = projections.at(entry.slot);
            int projected_origin = centers.count(projected) ? centered(centers.at(projected)) : out.start_x;
            require(projected_origin == origin && scroll[8 - projected] == scroll[8 - entry.slot],
                    "Background display list and module use different camera projections.");
            p.x = entry.x + (int)std::round(out.start_x * p.scroll) - origin - (b.x - p.source.x1);
            p.y = entry.y - (b.y - p.source.y1); p.rank = ranks.at(entry.slot); p.bound = true;
            out.planes.push_back(p);
            report << entry.module << ": offset " << entry.x << ", " << entry.y << "; scroll " << p.scroll << "x; origin " << origin << "\n";
        }
        report << "Centering uses the current artwork's tight module bounds. Apply also loads a separate floor reference when its texture and palette are available. Runtime actors, perspective skew, palette effects and callbacks are not recreated. Unassigned artwork keeps its source positions.";
        out.report = report.str();
    } catch (const std::exception &e) { out.planes.clear(); out.error = e.what(); }
    return out;
}
} // namespace studio
