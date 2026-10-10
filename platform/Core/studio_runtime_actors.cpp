#include "Core/studio_runtime_actors.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace studio {
namespace {
// Minimal JSON reader: enough for sidecars, strict about structure.
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    const Json *get(const std::string &key) const {
        for (const auto &f : fields) if (f.first == key) return &f.second;
        return nullptr;
    }
};
struct Reader {
    const std::string &s;
    size_t i = 0;
    int depth = 0;
    void space() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool fail(std::string &error, const char *what) {
        error = std::string(what) + " at byte " + std::to_string(i) + ".";
        return false;
    }
    bool string(std::string &out, std::string &error) {
        if (i >= s.size() || s[i] != '"') return fail(error, "Expected a string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            if (e == 'n') out += '\n'; else if (e == 't') out += '\t'; else if (e == 'r') out += '\r';
            else if (e == 'b') out += '\b'; else if (e == 'f') out += '\f';
            else if (e == 'u') {
                if (i + 4 > s.size()) return fail(error, "Truncated escape");
                unsigned code = (unsigned)std::stoul(s.substr(i, 4), nullptr, 16); i += 4;
                if (code < 0x80) out += char(code);
                else if (code < 0x800) { out += char(0xc0 | (code >> 6)); out += char(0x80 | (code & 63)); }
                else { out += char(0xe0 | (code >> 12)); out += char(0x80 | ((code >> 6) & 63)); out += char(0x80 | (code & 63)); }
            } else out += e;
        }
        if (i >= s.size()) return fail(error, "Unterminated string");
        ++i;
        return true;
    }
    bool value(Json &out, std::string &error) {
        space();
        if (i >= s.size()) return fail(error, "Unexpected end of file");
        if (++depth > 32) return fail(error, "JSON is nested too deeply");
        char c = s[i];
        bool ok = true;
        if (c == '{') {
            out.kind = Json::Object; ++i; space();
            if (i < s.size() && s[i] == '}') ++i;
            else for (;;) {
                std::string key; Json item;
                space();
                if (!string(key, error)) return false;
                space();
                if (i >= s.size() || s[i++] != ':') return fail(error, "Expected ':'");
                if (!value(item, error)) return false;
                out.fields.emplace_back(std::move(key), std::move(item));
                space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail(error, "Expected ',' or '}'");
            }
        } else if (c == '[') {
            out.kind = Json::Array; ++i; space();
            if (i < s.size() && s[i] == ']') ++i;
            else for (;;) {
                Json item;
                if (!value(item, error)) return false;
                out.items.push_back(std::move(item));
                space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return fail(error, "Expected ',' or ']'");
            }
        } else if (c == '"') {
            out.kind = Json::String; ok = string(out.text, error);
        } else if (s.compare(i, 4, "true") == 0) { out.kind = Json::Bool; out.boolean = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { out.kind = Json::Bool; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else {
            size_t used = 0;
            try { out.number = std::stod(s.substr(i, 32), &used); } catch (...) { used = 0; }
            if (!used || !std::isfinite(out.number)) return fail(error, "Invalid JSON value");
            out.kind = Json::Number; i += used;
        }
        --depth;
        return ok;
    }
};
void read(const Json &o, const char *key, std::string &v) { if (auto *j = o.get(key); j && j->kind == Json::String) v = j->text; }
void read(const Json &o, const char *key, bool &v) { if (auto *j = o.get(key); j && j->kind == Json::Bool) v = j->boolean; }
void read(const Json &o, const char *key, double &v) { if (auto *j = o.get(key); j && j->kind == Json::Number) v = j->number; }
void read(const Json &o, const char *key, int &v) {
    if (auto *j = o.get(key); j && j->kind == Json::Number)
        v = (int)std::clamp(std::llround(j->number), -1000000ll, 1000000ll);
}
std::vector<int> ints(const Json &o, const char *key) {
    std::vector<int> out;
    if (auto *j = o.get(key); j && j->kind == Json::Array)
        for (const auto &item : j->items) out.push_back(item.kind == Json::Number ? (int)std::llround(item.number) : 0);
    return out;
}
std::string quote(const std::string &v) {
    std::string out = "\"";
    for (unsigned char c : v) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else if (c < 32) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
        else out += char(c);
    }
    return out + "\"";
}
std::string upper(std::string v) {
    for (char &c : v) c = (char)std::toupper((unsigned char)c);
    return v;
}
bool numeric_label(const std::string &label) {
    return !label.empty() && std::all_of(label.begin(), label.end(), [](unsigned char c) { return std::isxdigit(c) || c == 'x' || c == 'X'; }) &&
           std::isdigit((unsigned char)label[0]);
}
} // namespace

std::string runtime_actor_sidecar_path(const std::string &document_path) {
    auto path = fs::u8path(document_path);
    return (path.parent_path() / (path.stem().u8string() + ".runtime.json")).u8string();
}

std::string make_actor_code_label(const std::string &name) {
    std::string src = name.empty() ? "runtime_actor" : name, out;
    char first = src[0];
    if (!std::isalpha((unsigned char)first) && first != '_') out = "rt_";
    for (char c : src) {
        c = (char)std::tolower((unsigned char)c);
        out += std::isalnum((unsigned char)c) || c == '_' ? c : '_';
    }
    return out.substr(0, 63);
}

bool parse_runtime_actors(const std::string &text, RuntimeActorSet &out, std::string &error) {
    error.clear();
    Json root;
    Reader reader{text};
    if (!reader.value(root, error)) return false;
    reader.space();
    if (reader.i != text.size()) { error = "Unexpected data after the sidecar's JSON object."; return false; }
    if (root.kind != Json::Object) { error = "The sidecar is not a JSON object."; return false; }
    const Json *actors = root.get("actors");
    if (!actors || actors->kind != Json::Array) { error = "The sidecar has no \"actors\" list."; return false; }
    if (actors->items.size() > (size_t)kMaxRuntimeActors) { error = "Sidecars are limited to 64 actors."; return false; }
    RuntimeActorSet set;
    read(root, "stage", set.stage);
    for (size_t n = 0; n < actors->items.size(); ++n) {
        const Json &o = actors->items[n];
        if (o.kind != Json::Object) { error = "Actor " + std::to_string(n + 1) + " is not an object."; return false; }
        RuntimeActor a;
        char name[16]; std::snprintf(name, sizeof name, "actor_%02zu", n + 1); a.name = name;
        a.emit_bgnd_code = true; // Legacy loader default when the key is absent.
        read(o, "name", a.name); read(o, "trigger", a.trigger); read(o, "emit_bgnd_code", a.emit_bgnd_code);
        read(o, "code_pattern", a.code_pattern); read(o, "code_label", a.code_label);
        read(o, "insert_list", a.insert_list); read(o, "scroll_symbol", a.scroll_symbol);
        read(o, "sync_group", a.sync_group); read(o, "frame_driver", a.frame_driver);
        read(o, "group_role", a.group_role); read(o, "group_index", a.group_index);
        read(o, "sync_group_timing", a.sync_group_timing); read(o, "multipart_piece", a.multipart_piece);
        read(o, "part_index", a.part_index); read(o, "part_count", a.part_count);
        read(o, "parent_actor", a.parent_actor); read(o, "source_object", a.source_object);
        read(o, "replace_source", a.replace_source);
        a.screen_space_y = a.source_object < 0;
        read(o, "screen_space_y", a.screen_space_y);
        read(o, "x", a.x); read(o, "y", a.y); read(o, "layer", a.layer); read(o, "scroll", a.scroll);
        read(o, "frame_ticks", a.frame_ticks); read(o, "phase_ticks", a.phase_ticks);
        read(o, "delay_min_ticks", a.delay_min_ticks); read(o, "delay_max_ticks", a.delay_max_ticks);
        read(o, "motion_x", a.motion_x); read(o, "motion_y", a.motion_y);
        read(o, "loop", a.loop); read(o, "enabled", a.enabled); read(o, "hfl", a.hfl); read(o, "vfl", a.vfl);
        if (const Json *frames = o.get("frames"); frames && frames->kind == Json::Array)
            for (const auto &f : frames->items) a.frames.push_back({f.kind == Json::String ? f.text : ""});
        if (a.frames.empty()) {
            std::string asset; read(o, "asset", asset);
            a.frames.push_back({asset});
        }
        if (a.frames.size() > (size_t)kMaxRuntimeFrames) {
            error = a.name + " has more than 24 frames."; return false;
        }
        auto dx = ints(o, "frame_dx"), dy = ints(o, "frame_dy"), ticks = ints(o, "frame_ticks_override");
        auto hm = ints(o, "frame_hfl_mode"), vm = ints(o, "frame_vfl_mode");
        for (size_t f = 0; f < a.frames.size(); ++f) {
            auto &fr = a.frames[f];
            if (f < dx.size()) fr.dx = dx[f];
            if (f < dy.size()) fr.dy = dy[f];
            if (f < ticks.size()) fr.ticks = std::max(0, ticks[f]);
            if (f < hm.size()) fr.hfl_mode = std::clamp(hm[f], 0, 2);
            if (f < vm.size()) fr.vfl_mode = std::clamp(vm[f], 0, 2);
        }
        if (!std::isfinite(a.scroll) || std::abs(a.scroll) > 16) { error = a.name + " has an invalid scroll."; return false; }
        a.frame_ticks = std::max(1, a.frame_ticks);
        a.part_count = std::max(1, a.part_count);
        if (a.code_label.empty()) a.code_label = make_actor_code_label(a.name);
        if (a.code_pattern.empty()) a.code_pattern = "loop_sprite";
        if (a.insert_list.empty()) a.insert_list = "baklst4";
        if (a.scroll_symbol.empty()) a.scroll_symbol = "worldtlx4";
        if (a.frame_driver.empty()) a.frame_driver = "frame_a9";
        if (a.group_role.empty()) a.group_role = "solo";
        set.actors.push_back(std::move(a));
    }
    out = std::move(set);
    return true;
}

std::string write_runtime_actors(const RuntimeActorSet &set) {
    std::ostringstream f;
    auto b = [](bool v) { return v ? "true" : "false"; };
    auto arr = [&](const char *key, const RuntimeActor &a, int RuntimeActorFrame::*field, bool comma) {
        f << "      \"" << key << "\": [";
        for (size_t i = 0; i < a.frames.size(); ++i) f << (i ? ", " : "") << a.frames[i].*field;
        f << "]" << (comma ? "," : "") << "\n";
    };
    f << "{\n  \"version\": 3,\n  \"source_type\": \"bddview_runtime_actors\",\n  \"stage\": " << quote(set.stage)
      << ",\n  \"actors\": [\n";
    for (size_t i = 0; i < set.actors.size(); ++i) {
        const auto &a = set.actors[i];
        char scroll[32]; std::snprintf(scroll, sizeof scroll, "%.4f", a.scroll);
        f << "    {\n"
          << "      \"name\": " << quote(a.name) << ",\n"
          << "      \"enabled\": " << b(a.enabled) << ",\n"
          << "      \"trigger\": " << quote(a.trigger) << ",\n"
          << "      \"emit_bgnd_code\": " << b(a.emit_bgnd_code) << ",\n"
          << "      \"code_pattern\": " << quote(a.code_pattern) << ",\n"
          << "      \"code_label\": " << quote(a.code_label) << ",\n"
          << "      \"insert_list\": " << quote(a.insert_list) << ",\n"
          << "      \"scroll_symbol\": " << quote(a.scroll_symbol) << ",\n"
          << "      \"sync_group\": " << quote(a.sync_group) << ",\n"
          << "      \"frame_driver\": " << quote(a.frame_driver) << ",\n"
          << "      \"group_role\": " << quote(a.group_role) << ",\n"
          << "      \"group_index\": " << a.group_index << ",\n"
          << "      \"sync_group_timing\": " << b(a.sync_group_timing) << ",\n"
          << "      \"multipart_piece\": " << b(a.multipart_piece) << ",\n"
          << "      \"part_index\": " << a.part_index << ",\n"
          << "      \"part_count\": " << a.part_count << ",\n"
          << "      \"parent_actor\": " << quote(a.parent_actor) << ",\n"
          << "      \"source_object\": " << a.source_object << ",\n"
          << "      \"replace_source\": " << b(a.replace_source) << ",\n"
          << "      \"screen_space_y\": " << b(a.screen_space_y) << ",\n"
          << "      \"x\": " << a.x << ",\n"
          << "      \"y\": " << a.y << ",\n"
          << "      \"layer\": " << a.layer << ",\n"
          << "      \"scroll\": " << scroll << ",\n"
          << "      \"frame_ticks\": " << a.frame_ticks << ",\n"
          << "      \"phase_ticks\": " << a.phase_ticks << ",\n"
          << "      \"delay_min_ticks\": " << a.delay_min_ticks << ",\n"
          << "      \"delay_max_ticks\": " << a.delay_max_ticks << ",\n"
          << "      \"motion_x\": " << a.motion_x << ",\n"
          << "      \"motion_y\": " << a.motion_y << ",\n"
          << "      \"loop\": " << b(a.loop) << ",\n"
          << "      \"hfl\": " << b(a.hfl) << ",\n"
          << "      \"vfl\": " << b(a.vfl) << ",\n"
          << "      \"frames\": [";
        for (size_t k = 0; k < a.frames.size(); ++k) f << (k ? ", " : "") << quote(a.frames[k].label);
        f << "],\n";
        arr("frame_dx", a, &RuntimeActorFrame::dx, true);
        arr("frame_dy", a, &RuntimeActorFrame::dy, true);
        arr("frame_ticks_override", a, &RuntimeActorFrame::ticks, true);
        arr("frame_hfl_mode", a, &RuntimeActorFrame::hfl_mode, true);
        arr("frame_vfl_mode", a, &RuntimeActorFrame::vfl_mode, false);
        f << "    }" << (i + 1 < set.actors.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    return f.str();
}

int actor_frame_ticks(const RuntimeActor &actor, size_t frame) {
    if (frame < actor.frames.size() && actor.frames[frame].ticks > 0) return actor.frames[frame].ticks;
    return std::max(1, actor.frame_ticks);
}

int actor_cycle_ticks(const RuntimeActor &actor) {
    int total = 0;
    for (size_t i = 0; i < actor.frames.size(); ++i) total += actor_frame_ticks(actor, i);
    return std::max(1, total);
}

ActorPose actor_pose(const RuntimeActorSet &set, size_t index, int64_t ticks, bool simulate) {
    ActorPose pose;
    if (index >= set.actors.size()) return pose;
    const auto &a = set.actors[index];
    if (!a.enabled || a.frames.empty()) return pose;
    // Sync followers share the leader's phase: an explicit leader wins, then the lowest index.
    const RuntimeActor *basis = &a;
    if (a.sync_group_timing && !a.sync_group.empty()) {
        long best = 0x7fffffff;
        for (const auto &c : set.actors) {
            if (!c.enabled || c.sync_group != a.sync_group) continue;
            long score = c.group_index - (c.group_role == "leader" ? 1000000 : 0);
            if (score < best) { best = score; basis = &c; }
        }
    }
    int64_t t = std::max<int64_t>(0, ticks);
    if (simulate) {
        if (a.delay_min_ticks > 0) {
            if (t < a.delay_min_ticks) return pose;
            t -= a.delay_min_ticks;
        }
    }
    int64_t local = std::max<int64_t>(0, t + basis->phase_ticks);
    size_t frame = 0;
    if (!(simulate && a.trigger == "always_static") && a.frames.size() > 1) {
        int64_t total = actor_cycle_ticks(a), cursor = 0;
        if (a.loop) local %= total;
        if (local >= total) frame = a.frames.size() - 1;
        else for (size_t i = 0; i < a.frames.size(); ++i) {
            int64_t step = actor_frame_ticks(a, i);
            if (local < cursor + step) { frame = i; break; }
            cursor += step;
        }
    }
    const auto &fr = a.frames[frame];
    pose.visible = true;
    pose.frame = frame;
    pose.hflip = fr.hfl_mode == 0 ? a.hfl : fr.hfl_mode == 2;
    pose.vflip = fr.vfl_mode == 0 ? a.vfl : fr.vfl_mode == 2;
    double moving = (double)(t + basis->phase_ticks);
    int mx = a.motion_x ? (int)std::fmod(a.motion_x / 65536.0 * moving, 1500.0) : 0;
    int my = a.motion_y ? (int)std::fmod(a.motion_y / 65536.0 * moving, 1500.0) : 0;
    pose.x = a.x + fr.dx + mx;
    pose.y = a.y + fr.dy + my;
    return pose;
}

int64_t actor_tick_for_frame(const RuntimeActorSet &set, size_t index, size_t frame, bool simulate) {
    if (index >= set.actors.size()) return -1;
    const auto &a = set.actors[index];
    if (!a.enabled || frame >= a.frames.size() || (simulate && a.trigger == "always_static" && frame != 0))
        return -1;
    const RuntimeActor *basis = &a;
    if (a.sync_group_timing && !a.sync_group.empty()) {
        long best = 0x7fffffff;
        for (const auto &candidate : set.actors) {
            if (!candidate.enabled || candidate.sync_group != a.sync_group) continue;
            long score = candidate.group_index - (candidate.group_role == "leader" ? 1000000 : 0);
            if (score < best) { best = score; basis = &candidate; }
        }
    }
    int64_t first_visible_tick = simulate ? a.delay_min_ticks : 0;
    auto first_pose = actor_pose(set, index, first_visible_tick, simulate);
    if (first_pose.visible && first_pose.frame == frame) return first_visible_tick;
    int64_t local = 0;
    for (size_t i = 0; i < frame; ++i) local += actor_frame_ticks(a, i);
    if (a.loop) {
        int64_t total = actor_cycle_ticks(a);
        if (local < basis->phase_ticks) {
            int64_t difference = basis->phase_ticks - local;
            local += ((difference + total - 1) / total) * total;
        }
    } else if (local < basis->phase_ticks) return -1;
    int64_t tick = local - basis->phase_ticks + (simulate ? a.delay_min_ticks : 0);
    if (tick < 0) return -1;
    auto pose = actor_pose(set, index, tick, simulate);
    return pose.visible && pose.frame == frame ? tick : -1;
}

std::vector<size_t> actor_replaced_sources(const RuntimeActorSet &set, int64_t ticks,
                                           bool simulate, size_t object_count) {
    std::vector<size_t> out;
    for (size_t i = 0; i < set.actors.size(); ++i) {
        const auto &actor = set.actors[i];
        if (!actor.replace_source || actor.source_object < 0 ||
            (size_t)actor.source_object >= object_count || !actor_pose(set, i, ticks, simulate).visible)
            continue;
        out.push_back((size_t)actor.source_object);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string actor_bgnd_skeleton(const RuntimeActor &a) {
    std::string label = make_actor_code_label(a.code_label.empty() ? a.name : a.code_label);
    const std::string &list = a.insert_list.empty() ? std::string("baklst4") : a.insert_list;
    unsigned packed = ((unsigned(a.y) & 0xffffu) << 16) | (unsigned(a.x) & 0xffffu);
    std::ostringstream o;
    char hex[16]; std::snprintf(hex, sizeof hex, "0%08xh", packed);
    o << "; Runtime actor: " << a.name << "\n"
      << "; Pattern: " << a.code_pattern << ", driver: " << a.frame_driver << ", trigger: " << a.trigger << "\n";
    char scroll[32]; std::snprintf(scroll, sizeof scroll, "%.4f", a.scroll);
    o << "; Insert: " << list << ", display scroll: " << a.scroll_symbol << ", preview scroll " << scroll << "\n";
    if (a.screen_space_y) o << "; Y is screen-space, matching BGND set_xy_coordinates display-object placement\n";
    if (!a.sync_group.empty()) o << "; Sync group: " << a.sync_group << "\n";
    if (a.sync_group_timing) o << "; Group timing is phase-locked to group leader\n";
    if (a.delay_min_ticks || a.delay_max_ticks)
        o << "; Delay window: " << a.delay_min_ticks << ".." << a.delay_max_ticks << " ticks\n";
    if (a.motion_x || a.motion_y) o << "; Motion hint: oxvel=" << a.motion_x << " oyvel=" << a.motion_y << "\n";
    o << "; call from the stage calla with: create pid_bani," << label << "_proc\n\n"
      << label << "_proc\n"
      << "\tmovi\t" << (a.frames.empty() || a.frames[0].label.empty() ? "FRAME1" : a.frames[0].label) << ",a5\n"
      << "\tcalla\tgso_dmawnz\n"
      << "\tmovi\t" << hex << ",a4\t\t; y=" << a.y << " x=" << a.x << " from bddtool\n"
      << "\tcalla\tset_xy_coordinates\n";
    if (a.hfl) o << "\tcalla\tflip_single\n";
    o << "\tmove\ta8,a0\n\tmovi\t" << list << ",b4\n\tcalla\tinsobj_v\n\n"
      << label << "_loop\n\tmovi\t" << label << "_ani,a9\n"
      << "\tmovk\t" << std::max(1, a.frame_ticks) << ",a0\t\t; frame ticks\n"
      << "\t; " << a.frame_driver << ": verify final driver against BGND.ASM pattern before commit\n"
      << "\tjruc\t" << label << "_loop\n\n" << label << "_ani\n";
    for (size_t i = 0; i < a.frames.size(); ++i) {
        const auto &f = a.frames[i];
        o << "\t.long\t" << (f.label.empty() ? "0" : f.label) << "\t\t; dx=" << f.dx << " dy=" << f.dy
          << " ticks=" << actor_frame_ticks(a, i) << " hfl=" << f.hfl_mode << " vfl=" << f.vfl_mode << "\n";
    }
    o << (a.loop ? "\t.long\tani_jump," + label + "_ani\n" : std::string("\t.long\t0\n"));
    return o.str();
}

RuntimeArtIndex index_runtime_art(const std::string &directory) {
    RuntimeArtIndex out;
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(fs::u8path(directory), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && upper(it->path().extension().u8string()) == ".IMG") files.push_back(it->path());
    std::sort(files.begin(), files.end()); // First file wins for duplicate labels, deterministically.
    for (const auto &path : files) {
        auto library = inspect_animation_library(path.u8string());
        ++out.scanned;
        if (!library.error.empty()) { ++out.unreadable; continue; }
        for (const auto &image : library.images)
            if (image.problem.empty()) out.files.emplace(upper(image.label), path.u8string());
    }
    return out;
}

const AnimationFrame *RuntimeArt::find(const std::string &label, size_t *source) const {
    auto it = frames.find(upper(label));
    if (it == frames.end() || it->second.first >= sources.size()) return nullptr;
    const auto &s = sources[it->second.first];
    if (it->second.second >= s.frames.size()) return nullptr;
    if (source) *source = it->second.first;
    return &s.frames[it->second.second];
}

RuntimeArt load_runtime_art(const RuntimeArtIndex &index, const RuntimeActorSet &set) {
    RuntimeArt out;
    std::map<std::string, std::vector<std::string>> by_file;
    std::set<std::string> seen;
    for (const auto &a : set.actors)
        for (const auto &f : a.frames) {
            auto label = upper(f.label);
            if (label.empty() || !seen.insert(label).second) continue;
            auto it = index.files.find(label);
            if (it != index.files.end()) by_file[it->second].push_back(label);
            else out.missing.push_back(numeric_label(label) ? label + " (numeric image indexes are not resolved)" : label);
        }
    for (const auto &entry : by_file)
        for (size_t start = 0; start < entry.second.size(); start += 128) {
            std::vector<std::string> chunk(entry.second.begin() + start,
                                           entry.second.begin() + std::min(entry.second.size(), start + 128));
            auto art = load_animation_selection(entry.first, chunk);
            if (!art.ready()) { out.errors.push_back(fs::u8path(entry.first).filename().u8string() + ": " + art.notice); continue; }
            for (size_t i = 0; i < chunk.size() && i < art.sequence.size(); ++i)
                out.frames[chunk[i]] = {out.sources.size(), (size_t)art.sequence[i]};
            out.sources.push_back(std::move(art));
        }
    return out;
}

} // namespace studio
