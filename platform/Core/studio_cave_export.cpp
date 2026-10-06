#include "Core/studio_cave_export.h"
#include "Core/studio_optimizer.h"
#include "Core/studio_rom_receipt.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
namespace fs = std::filesystem;
void need(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    need(bool(in), "Cannot read " + p.u8string());
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path &p, const std::string &s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << s;
    out.close();
    need(bool(out), "Cannot write " + p.u8string());
}
std::string fingerprint(const std::string &s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s)
        h = (h ^ c) * 1099511628211ULL;
    std::ostringstream out;
    out << std::hex << h;
    return out.str();
}
std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
int slot(const Plane &p) {
    const auto n = lower(p.source.name);
    if (n == "mk3cave4")
        return 4;
    if (n == "mk3cave3")
        return 3;
    if (n == "mk3cave2")
        return 5;
    if (n == "mk3cave1")
        return 2;
    throw std::runtime_error("MK3CAVE export requires its four original modules.");
}
std::map<std::string, int> equates(const fs::path &root) {
    auto s = read(root / "src/MK3CAVBG.ASM");
    std::regex line(
        R"((?:^|\n)(mk3c_[A-Za-z0-9_]+)\s+\.set\s+(-?[0-9]+|[0-9A-Fa-f]+[Hh])\s*(?:;[^\n]*)?(?=\n|$))");
    std::map<std::string, int> out;
    for (std::sregex_iterator i(s.begin(), s.end(), line), end; i != end; ++i) {
        auto v = (*i)[2].str();
        bool hex = v.back() == 'H' || v.back() == 'h';
        if (hex)
            v.pop_back();
        need(out.emplace((*i)[1].str(), std::stoi(v, nullptr, hex ? 16 : 10)).second,
             "Duplicate cave runtime constant.");
    }
    return out;
}
std::pair<int, int> origin(const Document &d, int plane) {
    int x = INT_MAX, y = INT_MAX;
    for (const auto &p : d.state().objects)
        if (p.plane == plane) {
            x = std::min(x, p.object.depth + p.runtime_dx);
            y = std::min(y, p.object.sy);
        }
    need(x != INT_MAX, "Empty MK3CAVE module.");
    return {x, y};
}
const char *hook =
#include "studio_cave_generator.inc"
    ;
const char *builder =
#include "studio_cave_build.inc"
    ;
std::string patched_generator(std::string source) {
    const std::string begin = "# BDDTOOL CAVE PROFILE BEGIN", end = "# BDDTOOL CAVE PROFILE END";
    auto b = source.find(begin);
    if (b != std::string::npos) {
        auto e = source.find(end, b);
        need(e != std::string::npos, "Incomplete bddtool generator adapter.");
        source.erase(b, e + end.size() - b);
    }
    need(
        source.find("MK3CAVE.PACK.json") == std::string::npos,
        "This checkout uses the old fixed validation candidate. Export to a normal game checkout.");
    auto main = source.find("if __name__ == \"__main__\":");
    need(main != std::string::npos && source.find("def water_spans(") != std::string::npos &&
             source.find("lambda x: x >= 600, -93, 0") != std::string::npos,
         "Unsupported MK3CAVE generator. Its source-coordinate/water contract needs review.");
    // The original preview only handles X flips; add the matching DMA Y flip.
    const std::string xflip = "t = t.transpose(Image.FLIP_LEFT_RIGHT)";
    auto at = source.find(xflip);
    if (at != std::string::npos && source.find("if bl[\"z\"] & 0x20:") == std::string::npos)
        source.insert(at + xflip.size(), "\n            if bl[\"z\"] & 0x20:\n                t = "
                                         "t.transpose(Image.FLIP_TOP_BOTTOM)");
    main = source.find("if __name__ == \"__main__\":");
    source.insert(main, std::string(hook) + "\n");
    return source;
}
void save_parts(const fs::path &folder, const std::string &name, const AssetBank &bank,
                const std::vector<size_t> &group) {
    need(!group.empty(),
         "Packing produced an empty cave slot; this generator requires four nonempty packs.");
    std::vector<BddCoreImage> images;
    std::vector<BddCoreObject> objects;
    int x = 0, h = 0;
    for (auto i : group) {
        auto im = bank.data.images[i];
        images.push_back(im);
        BddCoreObject o{};
        o.wx = 0x4000;
        o.depth = x;
        o.ii = im.idx;
        objects.push_back(o);
        x += im.w + 4;
        h = std::max(h, im.h);
    }
    std::ostringstream header, module;
    header << name << ' ' << x << ' ' << h << " 255 1 7 " << group.size();
    module << name << " 0 " << x - 5 << " 0 " << h - 1;
    std::ostringstream placement;
    placement << header.str() << '\n' << module.str() << '\n';
    for (const auto &o : objects)
        placement << "4000 " << o.depth << " 0 " << std::hex << std::uppercase << o.ii << std::dec
                  << " 0\n";
    write(folder / "data" / (name + ".BDB"), placement.str());
    std::ostringstream data;
    data << images.size() << '\n';
    for (const auto &im : images) {
        data << std::hex << im.idx << std::dec << ' ' << im.w << ' ' << im.h << ' ' << im.flags
             << '\n';
        data.write((const char *)im.pix.data(), (std::streamsize)im.pix.size());
    }
    for (const auto &pal : bank.data.palettes) {
        data << pal.name << ' ' << pal.count << '\n';
        for (int i = 0; i < pal.count; ++i) {
            data.put((char)(pal.rgb555[i] & 255));
            data.put((char)(pal.rgb555[i] >> 8));
        }
    }
    write(folder / "data" / (name + ".BDD"), data.str());
}
} // namespace

bool seed_cave_runtime(Document &d, const std::string &root_path, std::string &error) {
    try {
        if (d.state().runtime_profile == "mk3cave")
            return true;
        need(!d.has_layout() && !d.dirty() && d.notice().empty(),
             "Open MK3CAVE from its game checkout before editing; the existing layout has no "
             "custom runtime binding.");
        fs::path root = fs::u8path(root_path);
        need(lower(d.state().name) == "mk3cave" && d.state().planes.size() == 4,
             "Expected MK3CAVE's four source modules.");
        patched_generator(
            read(root / "tools/make_mk3cave.py")); // Validate supported generator contract.
        auto e = equates(root);
        int start = e.at("mk3c_start");
        auto planes = d.state().planes;
        std::vector<int> dx(d.state().objects.size());
        for (size_t i = 0; i < planes.size(); ++i) {
            auto &p = planes[i];
            auto n = lower(p.source.name);
            auto prefix = "mk3c_m" + n.substr(n.size() - 1) + "_";
            int mx = INT_MAX, my = INT_MAX;
            for (size_t j = 0; j < d.state().objects.size(); ++j) {
                const auto &o = d.state().objects[j];
                if (o.plane != (int)i)
                    continue;
                dx[j] = n == "mk3cave3" && o.object.depth >= 600 ? -93 : 0;
                mx = std::min(mx, o.object.depth + dx[j]);
                my = std::min(my, o.object.sy);
            }
            need(mx != INT_MAX, "Empty cave module.");
            p.scroll = e.at("mk3c_rate" + std::to_string(slot(p))) / 131072.0;
            p.x = e.at(prefix + "x") + (int)std::llround(start * p.scroll) - start -
                  (mx - p.source.x1);
            p.y = e.at(prefix + "y") - (my - p.source.y1);
            p.rank = n == "mk3cave4" ? 0 : n == "mk3cave3" ? 1 : n == "mk3cave2" ? 2 : 3;
            p.bound = true;
            p.locked = n == "mk3cave3";
        }
        d.seed_custom_runtime(planes, start, e.at("mk3c_worldy"), e.at("mk3c_ground"), dx,
                              "mk3cave");
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

bool prepare_cave_export(const Document &doc, const std::string &root_path,
                         const std::string &folder_path, GameExport &result, std::string &error) {
    try {
        auto root = fs::canonical(fs::u8path(root_path));
        auto folder = fs::weakly_canonical(fs::u8path(folder_path));
        need(!fs::exists(folder), "Choose a new export folder.");
        need(!doc.transaction_active() && doc.state().runtime_profile == "mk3cave",
             "Open MK3CAVE from its game checkout to bind the custom runtime before exporting.");
        need(!fs::exists(root / ".bddstudio-applying"),
             "An unfinished game apply needs recovery first.");
        auto policy = read_rom_slot_policy(root.u8string());
        need(policy.valid && policy.declared,
             policy.error.empty()
                 ? "Declare the four cave ROM slots in makevrom.py before exporting."
                 : policy.error);
        Document original;
        need(original.load((root / "data/MK3CAVE.BDB").u8string(), error), error);
        need(seed_cave_runtime(original, root.u8string(), error), error);
        const auto &s = doc.state(), &base = original.state();
        need(s.backdrop == -1, "This custom stage generator does not support backdrop color changes. Uncheck Set stage color before exporting.");
        need(s.planes.size() == 4 && base.planes.size() == 4 &&
                 s.assets->data.palettes.size() == 7 && base.assets->data.palettes.size() == 7,
             "Keep MK3CAVE's four modules and seven palettes. Disable palette compaction for this "
             "stage.");
        for (const auto &o : s.objects) {
            need(o.plane >= 0 && o.plane < 4, "Assign every cave object to an original layer.");
            int dx = lower(s.planes[o.plane].source.name) == "mk3cave3" && o.object.depth >= 600
                         ? -93
                         : 0;
            need(o.runtime_dx == dx, "A cave object lost its original-coordinate runtime binding.");
        }
        need(s.start_x == base.start_x && s.start_y == base.start_y && s.ground == base.ground,
             "This export preserves the existing cave camera, floor and water contract. Restore "
             "camera and ground settings.");
        for (size_t i = 0; i < 7; ++i) {
            auto &a = s.assets->data.palettes[i], &b = base.assets->data.palettes[i];
            need(a.count == b.count && std::strcmp(a.name, b.name) == 0 &&
                     std::equal(a.rgb555, a.rgb555 + a.count, b.rgb555),
                 "Keep the seven runtime palettes unchanged.");
        }
        need(s.objects.size() <= 250, "Cave export exceeds the conservative 250-placement limit.");
        for (size_t i = 0; i < 4; ++i) {
            const auto &p = s.planes[i], &q = base.planes[i];
            need(lower(p.source.name) == lower(q.source.name) && p.rank == q.rank &&
                     p.scroll == q.scroll,
                 "Keep the cave's runtime layer order and parallax rates; static artwork and layer "
                 "offsets can be edited.");
            need(p.source.x1 == q.source.x1 && p.source.y1 == q.source.y1 &&
                     p.source.x2 == q.source.x2 && p.source.y2 == q.source.y2,
                 "Cave source rectangles changed. Reopen the original-coordinate project.");
            if (lower(p.source.name) != "mk3cave3")
                continue;
            need(p.x == q.x && p.y == q.y,
                 "Restore the cavern/water layer position before export.");
            std::vector<const Placement *> a, b;
            for (const auto &o : s.objects)
                if (o.plane == (int)i)
                    a.push_back(&o);
            for (const auto &o : base.objects)
                if (o.plane == (int)i)
                    b.push_back(&o);
            need(a.size() == b.size(), "The cavern/water layer must retain its original pieces.");
            for (size_t j = 0; j < a.size(); ++j) {
                auto x = doc.image(a[j]->object.ii), y = original.image(b[j]->object.ii);
                need(x && y && x->w == y->w && x->h == y->h && x->pix == y->pix &&
                         a[j]->object.depth == b[j]->object.depth &&
                         a[j]->object.sy == b[j]->object.sy && a[j]->object.wx == b[j]->object.wx &&
                         a[j]->object.fl == b[j]->object.fl && a[j]->runtime_dx == b[j]->runtime_dx,
                     "The cavern/water artwork or placement changed; keep that layer protected.");
            }
        }
        GameExport out;
        out.root = root.u8string();
        out.folder = folder.u8string();
        out.label = "MK3CAVE_MOD";
        out.revision = s.revision;
        out.build_script = "tools/bddtool_mk3cave_build.py";
        auto dependency = [&](const std::string &rel) {
            auto path = fs::weakly_canonical(root / fs::u8path(rel));
            auto relative = path.lexically_relative(root);
            need(!relative.empty() && *relative.begin() != "..",
                 "Cave dependency escapes checkout.");
            out.dependencies.push_back({rel, read(path), {}, true});
        };
        for (const auto &rel :
             {"makevrom.py", "build.py", "makerom.py", "mamerom.py", "src/MK3CAVE.ASM",
              "data/FL_CAVE.BIN", "data/IMGBUILD.BAT", "dosbox_load2.conf", "dosbox_asm.conf"})
            dependency(rel);
        auto add = [&](const std::string &rel, const std::string &bytes) {
            auto p = fs::weakly_canonical(root / fs::u8path(rel));
            auto relative = p.lexically_relative(root);
            need(!relative.empty() && *relative.begin() != "..", "Cave output escapes checkout.");
            GameExportFile file{rel, {}, bytes, fs::exists(p)};
            if (file.existed)
                file.before = read(p);
            write(folder / fs::u8path(rel), bytes);
            out.files.push_back(std::move(file));
        };
        fs::create_directories(folder / "data");
        std::string runtime_report;
        need(doc.save_game_sources((folder / "data/MK3CAVE.BDB").u8string(), runtime_report, error),
             error);
        for (const auto &rel : {"data/MK3CAVE.BDB", "data/MK3CAVE.BDD", "data/MK3CAVE.BDD.meta",
                                "data/MK3CAVE.bddstudio"})
            add(rel, read(folder / rel));
        const auto &images = s.assets->data.images;
        std::vector<RomSlot> slots;
        std::vector<uint64_t> used(4), bits;
        std::vector<std::vector<size_t>> groups(4);
        for (int i = 0; i < 4; ++i) {
            auto name = "MK3CV" + std::to_string(i + 1);
            auto found = std::find_if(policy.slots.begin(), policy.slots.end(),
                                      [&](const RomSlot &p) { return p.name == name + ".IRW"; });
            need(found != policy.slots.end() && found->start >= 0x800000,
                 "Missing cave bank-1 ROM slot: " + name);
            slots.push_back(*found);
            dependency("data/" + name + ".LOD");
            auto lod = out.dependencies.back().before;
            std::smatch m;
            need(std::regex_search(lod, m, std::regex(R"(\*\*\*>\s*([0-9A-Fa-f]+)\s*,\s*1)")),
                 "Invalid cave LOD bank/base.");
            auto actual = std::stoull(m[1].str(), nullptr, 16);
            need(actual == 0x2000000 + (found->start - 0x800000) * 8,
                 "Cave LOD base differs from its declared ROM slot.");
        }
        for (const auto &im : images) {
            need(im.w > 0 && im.w <= 248 && im.w % 4 == 0 && im.h > 0,
                 "Cave images must have aligned widths of 4..248 pixels.");
            bits.push_back(optimization_image_bits(im));
        }
        std::vector<size_t> order(images.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return bits[a] > bits[b]; });
        for (auto i : order) {
            bool fit = false;
            for (int k = 0; k < 4; ++k)
                if (used[k] + bits[i] <= (slots[k].end - slots[k].start) * 8) {
                    used[k] += bits[i];
                    groups[k].push_back(i);
                    fit = true;
                    break;
                }
            need(fit, "The candidate does not fit the four current cave slots. Reduce artwork cost "
                      "and prepare again.");
        }
        std::ostringstream manifest, report;
        manifest << "{\n\"version\":1,\n\"sources\":{\"data/MK3CAVE.BDB\":\""
                 << fingerprint(read(folder / "data/MK3CAVE.BDB")) << "\",\"data/MK3CAVE.BDD\":\""
                 << fingerprint(read(folder / "data/MK3CAVE.BDD")) << "\",\"makevrom.py\":\""
                 << fingerprint(read(root / "makevrom.py")) << "\"},\n\"packs\":[";
        report << "MK3CAVE custom export\nOriginal source coordinates retained. Cavern/water and "
                  "all seven palettes protected.\n\n";
        report << runtime_report << '\n';
        for (int k = 0; k < 4; ++k) {
            auto name = "MK3CV" + std::to_string(k + 1);
            auto &g = groups[k];
            std::sort(g.begin(), g.end());
            save_parts(folder, name, *s.assets, g);
            for (auto ext : {".BDB", ".BDD"})
                add("data/" + name + ext, read(folder / "data" / (name + ext)));
            if (k)
                manifest << ',';
            manifest << "[\"" << name << "\"," << slots[k].start << ',' << slots[k].end << ",[";
            for (size_t j = 0; j < g.size(); ++j) {
                if (j)
                    manifest << ',';
                manifest << images[g[j]].idx;
            }
            manifest << "]]";
            report << name << ": " << used[k] / 8 << " estimated / "
                   << slots[k].end - slots[k].start << " reserved bytes\n";
        }
        manifest << "],\n\"bits\":{";
        for (size_t i = 0; i < images.size(); ++i) {
            if (i)
                manifest << ',';
            manifest << '"' << images[i].idx << "\":" << bits[i];
        }
        manifest << "},\n\"place\":{";
        for (size_t i = 0; i < 4; ++i) {
            auto p = s.planes[i];
            auto xy = origin(doc, (int)i);
            int x =
                p.x + xy.first - p.source.x1 + s.start_x - (int)std::llround(s.start_x * p.scroll);
            int y = p.y + xy.second - p.source.y1;
            need(x >= -32768 && x <= 32767 && y >= -32768 && y <= 32767,
                 "Cave placement exceeds runtime coordinate range.");
            if (i)
                manifest << ',';
            manifest << '"' << lower(p.source.name) << "\":[" << slot(p) << ',' << x << ',' << y
                     << ']';
            report << p.name << ": runtime offset " << x << ", " << y << "\n";
        }
        manifest << "}\n}\n";
        add("data/MK3CAVE.bddtool.json", manifest.str());
        add("tools/make_mk3cave.py", patched_generator(read(root / "tools/make_mk3cave.py")));
        add("tools/bddtool_mk3cave_build.py", builder);
        report << "\nApply updates source art, the four pack sidecars, the generator adapter and "
                  "its build helper with backups. "
                  "Build & verify ROMs runs LOAD2, checks every packed pixel and slot, assembles "
                  "the game, and writes "
                  "rom/bddtool/mk2.zip without installing it. Runtime combat and emulator checks "
                  "remain separate.\n";
        out.report = report.str();
        write(folder / "REVIEW.txt", out.report);
        for (const auto &d : out.dependencies)
            need(read(root / d.relative) == d.before,
                 "Game dependency changed during preparation.");
        result = std::move(out);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
} // namespace studio
