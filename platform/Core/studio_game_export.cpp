#include "Core/studio_game_export.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace fs = std::filesystem;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string upper(std::string value) {
    for (char &c : value)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return value;
}
std::string trim(std::string value) {
    auto a = value.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : value.substr(a, value.find_last_not_of(" \t\r\n") - a + 1);
}
std::string code(const std::string &line) {
    auto s = trim(line.substr(0, line.find(';')));
    return !s.empty() && s[0] == '*' ? "" : s;
}
std::vector<std::string> operands(const std::string &line, const char *directive) {
    auto s = code(line);
    auto n = s.find_first_of(" \t");
    if (n == std::string::npos || upper(s.substr(0, n)) != upper(directive))
        return {};
    std::istringstream in(s.substr(n));
    std::string token;
    std::vector<std::string> out;
    while (std::getline(in, token, ','))
        out.push_back(trim(token));
    return out;
}
std::string label(const std::string &line) {
    if (line.empty() || line[0] == ' ' || line[0] == '\t')
        return {};
    auto s = code(line);
    auto end = s.find_first_of(" :\t\r");
    auto token = s.substr(0, end);
    static const std::regex symbol("[A-Za-z_][A-Za-z_0-9]*");
    return std::regex_match(token, symbol) ? upper(token) : "";
}
bool ends(const std::string &text, const std::string &suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}
struct Source {
    std::vector<std::string> lines;
    std::map<std::string, size_t> labels;
    std::set<std::string> duplicates;
    bool newline = false;
    explicit Source(const std::string &bytes) {
        newline = !bytes.empty() && bytes.back() == '\n';
        std::istringstream in(bytes);
        std::string line;
        while (std::getline(in, line)) {
            auto name = label(line);
            if (!name.empty() && !labels.emplace(name, lines.size()).second)
                duplicates.insert(name);
            lines.push_back(line);
        }
    }
    size_t at(const std::string &name) const {
        check(!duplicates.count(upper(name)), "Duplicate assembly label: " + name);
        auto it = labels.find(upper(name));
        check(it != labels.end(), "Missing assembly label: " + name);
        return it->second;
    }
    size_t end(size_t begin) const {
        for (size_t i = begin + 1; i < lines.size(); i++)
            if (!label(lines[i]).empty())
                return i;
        return lines.size();
    }
    void set(size_t row, const char *op, const std::string &value) {
        const auto old = lines[row];
        auto semi = old.find(';');
        std::string comment = semi == std::string::npos ? "" : "\t" + trim(old.substr(semi));
        lines[row] = "\t" + std::string(op) + "\t" + value + comment +
                     (!old.empty() && old.back() == '\r' ? "\r" : "");
    }
    std::string text() const {
        std::string result;
        for (size_t i = 0; i < lines.size(); i++) {
            result += lines[i];
            if (i + 1 < lines.size() || newline)
                result += '\n';
        }
        return result;
    }
};
struct Binding {
    std::string module;
    int slot = 0;
    size_t offset = 0;
    bool centered = false;
};
struct Stage {
    std::string name, scroll, dlists;
    std::vector<size_t> words;
    std::vector<Binding> bindings;
};
Stage parse_stage(const Source &source, const std::string &name) {
    Stage stage;
    stage.name = name;
    size_t begin = source.at(name), end = source.end(begin);
    int longs = 0, slot = 0;
    bool center = false, finished = false;
    std::set<std::string> modules;
    for (size_t i = begin + 1; i < end; i++) {
        auto w = operands(source.lines[i], ".word"), l = operands(source.lines[i], ".long");
        if (!longs && !w.empty()) {
            check(w.size() == 1, "Grouped stage header words are unsupported: " + name);
            stage.words.push_back(i);
        }
        if (l.empty())
            continue;
        auto token = upper(l[0]);
        longs++;
        if (longs <= 4) {
            check(l.size() == 1, "Grouped stage header longs are unsupported: " + name);
            if (longs == 2)
                stage.scroll = token;
            if (longs == 3)
                stage.dlists = token;
            continue;
        }
        if (token == "0" || token == ">FFFFFFFF" || token == "0FFFFFFFFH") {
            finished = true;
            break;
        }
        if (token == "CENTER_X") {
            check(!center, "Repeated CENTER_X in " + name);
            center = true;
            continue;
        }
        if (center) {
            check(l.size() == 2 && ends(token, "BMOD"), "Unsupported center_x record in " + name);
            auto module = token.substr(0, token.size() - 4);
            bool found = false;
            for (auto &binding : stage.bindings)
                if (binding.module == module) {
                    auto world = "WORLDTLX" +
                                 (binding.slot == 1 ? std::string{} : std::to_string(binding.slot));
                    check(upper(l[1]) == world,
                          "Centering targets a different world pointer: " + module);
                    binding.centered = true;
                    found = true;
                }
            check(found, "Centering references an unknown module: " + module);
            continue;
        }
        check(l.size() == 1 && ++slot <= 8, "Unsupported background plane list in " + name);
        if (token == "SKIP_BAKMOD")
            continue;
        check(ends(token, "BMOD"), "Unsupported stage initialization directive: " + token);
        std::string module = token.substr(0, token.size() - 4);
        check(modules.insert(module).second, "Repeated runtime module: " + module);
        size_t offset = i + 1;
        while (offset < end && code(source.lines[offset]).empty())
            offset++;
        check(offset < end && operands(source.lines[offset], ".word").size() == 2,
              "Missing plane offset for " + module);
        stage.bindings.push_back({module, slot, offset, false});
    }
    check(finished && stage.words.size() == 6 && longs >= 4 && !stage.bindings.empty(),
          "Unsupported stage header/list in " + name);
    return stage;
}
std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Cannot read " + path.u8string());
    std::string result((std::istreambuf_iterator<char>(in)), {});
    check(!in.bad(), "Read failed: " + path.u8string());
    return result;
}
void write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), (std::streamsize)bytes.size());
    out.close();
    check(bool(out), "Cannot write " + path.u8string());
}
fs::path target(const fs::path &root, const std::string &relative) {
    auto p = fs::weakly_canonical(root / fs::u8path(relative));
    auto rel = p.lexically_relative(root);
    check(!rel.empty() && !rel.is_absolute() && *rel.begin() != "..",
          "Export target escapes selected game folder.");
    return p;
}
std::string hexrate(long rate) {
    std::ostringstream out;
    out << '0' << std::hex << rate << 'h';
    return out.str();
}
} // namespace

bool export_game_assembly(const Document &doc, const std::string &bytes, AssemblyExport &result,
                          std::string &error, const std::string &requested) {
    try {
        Source source(bytes);
        const auto &state = doc.state();
        Stage stage;
        int score = 0;
        bool tied = false;
        std::set<std::string> names;
        for (const auto &p : state.planes)
            check(names.insert(upper(p.source.name)).second, "Duplicate source module name.");
        if (!requested.empty())
            stage = parse_stage(source, upper(requested));
        else {
            for (const auto &entry : source.labels)
                if (ends(entry.first, "_MOD")) {
                    try {
                        auto candidate = parse_stage(source, entry.first);
                        int matches = 0;
                        for (const auto &binding : candidate.bindings)
                            matches += (int)names.count(binding.module);
                        if (matches > score) {
                            stage = candidate;
                            score = matches;
                            tied = false;
                        } else if (matches && matches == score)
                            tied = true;
                    } catch (const std::runtime_error
                                 &) { /* Another stage may use a different initializer. */
                    }
                }
            check(score > 0, "No supported runtime stage matches these module names. Choose an "
                             "existing stage definition.");
            check(!tied,
                  "Several stage definitions match. Enter the exact stage label before preparing.");
        }
        check(!doc.transaction_active(), "Finish the current edit before exporting.");
        check(state.start_x >= -32768 && state.start_x <= 32767 && state.start_y >= -32768 &&
                  state.start_y <= 32767 && state.ground >= -32768 && state.ground <= 32767,
              "Camera or ground exceeds the game's signed 16-bit coordinates.");
        // Shared tables would change other stages. Refuse rather than silently alter their
        // behavior.
        for (const auto &table : {stage.scroll, stage.dlists}) {
            int refs = 0;
            for (const auto &line : source.lines) {
                auto l = operands(line, ".long");
                if (!l.empty() && upper(l[0]) == table)
                    refs++;
            }
            check(refs == 1, "Runtime table " + table +
                                 " is shared; give this stage a private table before export.");
        }
        std::vector<size_t> rates;
        auto scroll = source.at(stage.scroll);
        for (size_t i = scroll + 1; i < source.end(scroll); i++) {
            auto l = operands(source.lines[i], ".long");
            if (!l.empty()) {
                check(l.size() == 1, "Grouped scroll rates are unsupported.");
                rates.push_back(i);
            }
        }
        check(rates.size() == 9, "Expected nine scroll rates in " + stage.scroll);
        std::map<int, size_t> draw_rows;
        std::vector<size_t> slots;
        auto dl = source.at(stage.dlists);
        bool terminated = false;
        static const std::regex bak("BAKLST([1-8])");
        for (size_t i = dl + 1; i < source.end(dl); i++) {
            auto l = operands(source.lines[i], ".long");
            if (l.empty())
                continue;
            if (l.size() == 1 && l[0] == "0") {
                terminated = true;
                break;
            }
            auto token = upper(l[0]);
            std::smatch match;
            if (std::regex_match(token, match, bak)) {
                int slot = std::stoi(match[1]);
                check(l.size() == 2 && draw_rows.emplace(slot, i).second,
                      "Repeated/malformed background draw row.");
                slots.push_back(i);
            }
        }
        check(terminated, "Display list has no terminator.");
        check(draw_rows.size() == stage.bindings.size(),
              "The display list has unbound or missing planes; resolve them before export.");
        AssemblyExport out;
        out.label = stage.name;
        std::ostringstream report;
        report << "Stage " << stage.name << "\nCamera start " << state.start_x << ", "
               << state.start_y << "; ground " << state.ground << "\n";
        struct Ranked {
            int rank;
            size_t original;
        };
        std::vector<Ranked> order;
        std::set<std::string> bound;
        std::set<int> width_warnings;
        for (const auto &binding : stage.bindings) {
            auto it = std::find_if(state.planes.begin(), state.planes.end(), [&](const Plane &p) {
                return upper(p.source.name) == binding.module;
            });
            check(it != state.planes.end(),
                  "Runtime module is missing from the document: " + binding.module);
            const auto &p = *it;
            int plane = (int)(it - state.planes.begin());
            bound.insert(binding.module);
            check(draw_rows.count(binding.slot) != 0, "No draw row for " + binding.module);
            check(std::isfinite(p.scroll) && p.scroll >= 0 && p.scroll <= 16,
                  "Unsupported parallax for " + binding.module + " (allowed: 0 to 16).");
            int minx = 0, miny = 0, maxx = 0, maxy = 0, count = 0;
            for (const auto &obj : state.objects)
                if (obj.plane == plane) {
                    const auto *im = doc.image(obj.object.ii);
                    check(im != nullptr, "Missing image in " + binding.module);
                    check(im->w > 0 && im->h > 0, "Empty image in " + binding.module);
                    if ((im->w > 250 || im->w % 4) && width_warnings.insert(im->idx).second)
                        report << "REVIEW: image " << im->idx << " has width " << im->w
                               << "; MK2 expects a multiple of 4, no more than 250 pixels. Verify "
                                  "LOAD2 output.\n";
                    int x = obj.object.depth, y = obj.object.sy;
                    if (!count++) {
                        minx = x;
                        miny = y;
                        maxx = x + im->w;
                        maxy = y + im->h;
                    } else {
                        minx = std::min(minx, x);
                        miny = std::min(miny, y);
                        maxx = std::max(maxx, x + im->w);
                        maxy = std::max(maxy, y + im->h);
                    }
                }
            check(count > 0, "Runtime plane is empty: " + binding.module);
            check(maxx - minx <= 32767 && maxy - miny <= 32767,
                  "Module dimensions exceed signed 16-bit range: " + binding.module);
            long rate = (long)std::llround(p.scroll * 131072);
            double factor = rate / 131072.0;
            int origin = binding.centered ? (maxx - minx) / 2 - 199 : state.start_x;
            int x = (int)std::llround(p.x + (minx - p.source.x1) + origin - state.start_x * factor);
            int y = p.y + (miny - p.source.y1);
            check(x >= -32768 && x <= 32767 && y >= -32768 && y <= 32767,
                  "Runtime offset exceeds signed 16-bit range: " + binding.module);
            source.set(binding.offset, ".word", std::to_string(x) + "," + std::to_string(y));
            source.set(rates[8 - binding.slot], ".long", hexrate(rate));
            out.planes.push_back({binding.module, binding.slot, x, y, origin, factor});
            order.push_back({p.rank, draw_rows.at(binding.slot)});
            report << binding.module << ": baklst" << binding.slot << ", offset " << x << ", " << y
                   << ", parallax " << std::setprecision(8) << factor << "x\n";
        }
        for (const auto &p : state.planes)
            if (!bound.count(upper(p.source.name))) {
                check(!p.bound, "Layer " + p.name +
                                    " lost its runtime binding. Bind it in the specialist editor "
                                    "first.");
                report << "REVIEW: " << p.source.name
                       << " is an unused source layer. Its artwork and local transforms are not "
                          "shown in game; no runtime binding is added.\n";
            }
        for (const auto &obj : state.objects)
            check(obj.plane >= 0, "Assign unassigned artwork to a layer before game export.");
        std::stable_sort(order.begin(), order.end(),
                         [](const Ranked &a, const Ranked &b) { return a.rank < b.rank; });
        std::vector<std::string> reordered;
        for (auto row : order)
            reordered.push_back(source.lines[row.original]);
        for (size_t i = 0; i < slots.size(); i++)
            source.lines[slots[i]] = reordered[i];
        source.set(stage.words[1], ".word", std::to_string(state.ground));
        source.set(stage.words[2], ".word", std::to_string(state.start_y));
        source.set(stage.words[3], ".word", std::to_string(state.start_x));
        report << "Draw order (back to front):";
        for (const auto &line : reordered)
            report << ' ' << operands(line, ".long")[0];
        report << "\n";
        report
            << "Fighter, shadow, floor and actor entries retain their display-list slots.\n"
            << "Visibility, locks and solo are editor controls; all placed artwork is exported.\n"
            << "Offsets include LOAD2's tight bounds and center_x compensation. Fixed-point rates "
               "and offsets are rounded to game precision.\n";
        out.text = source.text();
        out.report = report.str();
        result = std::move(out);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

bool prepare_game_export(const Document &document, const std::string &game_root,
                         const std::string &package_folder, GameExport &result, std::string &error,
                         const std::string &stage_label) {
    try {
        const auto root = fs::canonical(fs::u8path(game_root));
        const auto folder = fs::weakly_canonical(fs::u8path(package_folder));
        check(!fs::exists(root / ".bddstudio-applying"),
              "The game checkout has an unfinished Studio apply. Follow "
              ".bddstudio-applying/RECOVERY.txt first.");
        const auto name = document.state().name;
        check(std::regex_match(name, std::regex("[A-Za-z_][A-Za-z0-9_]{0,7}")),
              "Game export requires a stage name of up to eight letters, digits or underscores.");
        const std::string stem = "data/" + name;
        auto asm_path = target(root, "src/BGND.ASM");
        check(fs::is_regular_file(asm_path) && fs::is_regular_file(target(root, stem + ".BDB")) &&
                  fs::is_regular_file(target(root, stem + ".BDD")),
              "Choose an existing game checkout containing src/BGND.ASM and data/" + name +
                  ".BDB/.BDD.");
        BddCoreStage existing;
        check(bdd_core_stage_load_bdb(&existing, target(root, stem + ".BDB").u8string().c_str()) !=
                  0,
              existing.error);
        std::set<std::string> existing_modules;
        for (const auto &module : existing.bdb.modules)
            existing_modules.insert(upper(module.name));
        for (const auto &plane : document.state().planes)
            check(existing_modules.count(upper(plane.source.name)) != 0,
                  "New module " + std::string(plane.source.name) +
                      " needs a runtime binding and LOAD2 integration before export.");
        bool lod = false;
        for (const auto &entry : fs::directory_iterator(root / "data"))
            if (upper(entry.path().extension().u8string()) == ".LOD") {
                std::istringstream lines(read(entry.path()));
                std::string line;
                while (std::getline(lines, line)) {
                    std::istringstream fields(code(line));
                    std::string op, value;
                    fields >> op >> value;
                    if (upper(op) == "BBB>" && upper(value) == upper(name))
                        lod = true;
                }
            }
        check(lod, "No LOAD2 BBB> entry references " + name +
                       " in this checkout. Add its LOD integration first.");
        // Validate against the live model first, before source repacking adjusts exported origins.
        AssemblyExport preview;
        auto asm_before = read(asm_path);
        check(export_game_assembly(document, asm_before, preview, error, stage_label), error);
        check(!fs::exists(folder),
              "Choose a new export folder; an existing package will not be overwritten.");
        fs::create_directories(folder / "data");
        fs::create_directories(folder / "src");
        Document copy = document;
        check(copy.save((folder / fs::u8path(stem + ".BDB")).u8string(), error, true), error);
        // Produce ASM from the original live model: its scene is identical to the repacked copy,
        // but it still identifies untouched, unbound source modules correctly.
        write(folder / "src" / "BGND.ASM", preview.text);
        GameExport out;
        out.root = root.u8string();
        out.folder = folder.u8string();
        out.label = preview.label;
        out.requested_label = upper(stage_label);
        out.revision = document.state().revision;
        for (const auto &relative :
             std::vector<std::string>{stem + ".BDB", stem + ".BDD", stem + ".BDD.meta",
                                      stem + ".bddstudio", "src/BGND.ASM"}) {
            auto dest = target(root, relative);
            GameExportFile file;
            file.relative = relative;
            file.existed = fs::exists(dest);
            if (file.existed) {
                check(fs::is_regular_file(dest),
                      "Target is not a regular file: " + dest.u8string());
                file.before = read(dest);
                if (relative == "src/BGND.ASM")
                    check(file.before == asm_before,
                          "Game assembly changed while preparing the export. Prepare again.");
            }
            file.after = read(folder / fs::u8path(relative));
            out.files.push_back(std::move(file));
        }
        std::ostringstream report;
        report << preview.report << "\nGame folder: " << root.u8string() << "\n\nFiles to apply:\n";
        for (const auto &file : out.files)
            report << file.relative << (file.before == file.after ? " (unchanged)" : " (updated)")
                   << "\n";
        report
            << "\nApply updates these source files with backups. Then run the game's full build.py "
               "(LOAD2 + assembly), followed by its normal ROM packaging and emulator check.\n"
            << "A BBB> entry was found in data/*.LOD; the chosen game build must actually include "
               "that LOD. No ROM addresses or deployment paths are invented by Studio.\n";
        out.report = report.str();
        write(folder / "REVIEW.txt", out.report);
        std::ostringstream diff;
        Source before(asm_before), after(preview.text);
        for (size_t i = 0; i < before.lines.size(); i++)
            if (before.lines[i] != after.lines[i])
                diff << "@@ BGND.ASM line " << i + 1 << " @@\n-" << before.lines[i] << "\n+"
                     << after.lines[i] << "\n";
        write(folder / "BGND.diff", diff.str());
        result = std::move(out);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

bool apply_game_export(GameExport &package, std::string &error) {
    try {
        check(!package.applied && !package.files.empty(),
              "Prepare a fresh export before applying.");
        const auto root = fs::canonical(fs::u8path(package.root)),
                   folder = fs::canonical(fs::u8path(package.folder));
        auto lock = root / ".bddstudio-applying";
        check(!fs::exists(lock), "Another Studio apply is active or unfinished in this checkout.");
        check(!fs::exists(folder / "APPLYING.txt"),
              "This export has an unfinished apply transaction. Restore its backups first.");
        for (const auto &file : package.files) {
            auto dest = target(root, file.relative);
            check(fs::exists(dest) == file.existed && (!file.existed || read(dest) == file.before),
                  "Game source changed since review: " + file.relative + ". Prepare again.");
            check(read(target(folder, file.relative)) == file.after,
                  "Export package changed since review: " + file.relative + ". Prepare again.");
            check(!fs::exists(fs::path(dest.u8string() + ".studio-apply-tmp")),
                  "An unfinished temporary file exists for " + file.relative);
        }
        fs::create_directories(folder / "backups");
        std::ostringstream journal;
        journal << "Restore existing targets from backups; remove targets marked NEW.\n";
        for (const auto &file : package.files) {
            auto dest = target(root, file.relative);
            auto backup = folder / "backups" / fs::u8path(file.relative);
            fs::create_directories(backup.parent_path());
            if (file.existed)
                write(backup, file.before);
            journal << (file.existed ? "RESTORE " : "NEW ") << std::quoted(dest.u8string()) << ' '
                    << std::quoted(backup.u8string()) << '\n';
        }
        check(fs::create_directory(lock), "Another Studio apply is active in this checkout.");
        size_t installed = 0;
        try {
            write(lock / "RECOVERY.txt",
                  "Export package: " + folder.u8string() +
                      "\nRestore the files listed in APPLYING.txt from that package's backups. If "
                      "APPLIED.txt exists instead, replacement completed. Remove this marker "
                      "directory only after recovery.\n");
            write(folder / "APPLYING.txt", journal.str());
            for (const auto &file : package.files)
                write(fs::path(target(root, file.relative).u8string() + ".studio-apply-tmp"),
                      file.after);
            // Recheck after staging, immediately before replacing the first game source.
            for (const auto &file : package.files) {
                auto dest = target(root, file.relative);
                check(fs::exists(dest) == file.existed &&
                          (!file.existed || read(dest) == file.before),
                      "Game source changed during staging: " + file.relative);
            }
            for (const auto &file : package.files) {
                auto dest = target(root, file.relative);
                if (file.existed)
                    fs::remove(dest);
                installed++;
                fs::rename(fs::path(dest.u8string() + ".studio-apply-tmp"), dest);
            }
            fs::rename(folder / "APPLYING.txt", folder / "APPLIED.txt");
            package.applied = true;
            std::error_code cleanup;
            fs::remove(lock / "RECOVERY.txt", cleanup);
            fs::remove(lock, cleanup);
        } catch (...) {
            bool restored = true;
            for (size_t i = 0; i < installed; i++)
                try {
                    const auto &file = package.files[i];
                    auto dest = target(root, file.relative);
                    if (file.existed)
                        write(dest, file.before);
                    else
                        fs::remove(dest);
                } catch (...) {
                    restored = false;
                }
            for (const auto &file : package.files) {
                std::error_code ec;
                fs::remove(fs::path(target(root, file.relative).u8string() + ".studio-apply-tmp"),
                           ec);
            }
            if (restored) {
                fs::remove(folder / "APPLYING.txt");
                std::error_code cleanup;
                fs::remove(lock / "RECOVERY.txt", cleanup);
                fs::remove(lock, cleanup);
            }
            throw;
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
} // namespace studio
