#include "Core/studio_library_audit.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace studio {
namespace fs = std::filesystem;

static bool csv_row(const std::string &line, std::vector<std::string> &out) {
    out.clear(); std::string field; bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"' && field.empty()) quoted = true;
        else if (c == ',') { out.push_back(field); field.clear(); }
        else field += c;
    }
    if (quoted) return false;
    if (!field.empty() && field.back() == '\r') field.pop_back();
    out.push_back(field); return true;
}
static bool number(const std::string &text, int64_t &out) {
    if (text.empty()) { out = -1; return true; }
    try { size_t used = 0; out = std::stoll(text, &used); return used == text.size(); }
    catch (...) { return false; }
}
static bool safe_relative(const std::string &text) {
    auto path = fs::u8path(text);
    if (text.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto &part : path) if (part == "..") return false;
    return true;
}

bool load_library_audit(const std::string &folder, LibraryAuditReport &out, std::string &error) {
    out = {}; error.clear();
    auto root = fs::u8path(folder), path = root / "summary.csv";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { error = "Choose an audit report folder containing summary.csv."; return false; }
    auto size = fs::file_size(path, ec);
    if (ec) { error = "The selected folder does not contain summary.csv."; return false; }
    if (size > 1024 * 1024) { error = "summary.csv is larger than the 1 MiB report limit."; return false; }
    std::ifstream input(path, std::ios::binary);
    std::string line; std::vector<std::string> fields;
    if (!std::getline(input, line) || !csv_row(line, fields)) { error = "summary.csv has no valid header."; return false; }
    std::map<std::string, size_t> columns;
    for (size_t i = 0; i < fields.size(); ++i) columns[fields[i]] = i;
    const char *required[] = {"file", "scope", "baseline_bytes", "preserve_saved", "best_saved", "best_mode",
        "extra_objects", "extra_table_bytes", "extra_palette_bytes", "palettes_after", "unverified_modes", "detail"};
    for (const char *name : required) if (!columns.count(name)) { error = std::string("summary.csv is missing column: ") + name; return false; }
    auto value = [&](const char *name) -> const std::string & { return fields[columns[name]]; };
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        if (out.rows.size() >= 4096) { error = "summary.csv exceeds the 4,096-row report limit."; return false; }
        if (!csv_row(line, fields) || fields.size() != columns.size()) { error = "summary.csv contains a malformed row."; return false; }
        LibraryAuditRow row;
        row.file = value("file"); row.scope = value("scope"); row.best_mode = value("best_mode");
        row.unverified_modes = value("unverified_modes"); row.detail = value("detail");
        row.active = row.scope.find("active") != std::string::npos && row.scope.rfind("not ", 0) != 0;
        int64_t extra_objects, table, palette, palettes_after;
        if (!safe_relative(row.file) || !safe_relative(row.detail) || !number(value("baseline_bytes"), row.baseline_bytes) ||
            !number(value("preserve_saved"), row.preserve_saved) || !number(value("best_saved"), row.best_saved) ||
            !number(value("extra_objects"), extra_objects) || !number(value("extra_table_bytes"), table) ||
            !number(value("extra_palette_bytes"), palette) || !number(value("palettes_after"), palettes_after)) {
            error = "summary.csv contains an unsafe path or invalid number."; return false;
        }
        row.extra_objects = (int)extra_objects; row.extra_table_bytes = (int)table;
        row.extra_palette_bytes = (int)palette; row.palettes_after = (int)palettes_after;
        if (row.active && row.preserve_saved >= 0) out.preserve_total += row.preserve_saved;
        if (row.active && row.best_saved >= 0) out.best_total += row.best_saved;
        if (!row.unverified_modes.empty()) ++out.incomplete;
        out.rows.push_back(std::move(row));
    }
    if (!input.eof()) { error = "Could not finish reading summary.csv."; return false; }
    if (out.rows.empty()) { error = "summary.csv contains no audit results."; return false; }
    std::stable_sort(out.rows.begin(), out.rows.end(), [](const auto &a, const auto &b) {
        return std::tie(a.best_saved, a.file) > std::tie(b.best_saved, b.file);
    });
    out.folder = fs::absolute(root).u8string();
    return true;
}

bool prepare_library_audit_review(const LibraryAuditReport &report, const LibraryAuditRow &row,
                                  const std::string &destination, std::string &bdd_path, std::string &error) {
    bdd_path.clear(); error.clear();
    if (!safe_relative(row.file)) { error = "The audit row contains an unsafe input path."; return false; }
    auto source = fs::u8path(report.folder) / "inputs" / fs::u8path(row.file);
    auto output = fs::u8path(destination);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) { error = "The audit's frozen input copy is missing."; return false; }
    if (fs::exists(output, ec)) { error = "The scratch review folder already exists."; return false; }
    fs::create_directories(output, ec);
    if (ec) { error = "Cannot create the scratch review folder: " + ec.message(); return false; }
    auto lowercase = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return text;
    };
    auto wanted_stem = lowercase(source.stem().u8string());
    auto wanted_meta = lowercase(source.filename().u8string()) + ".meta";
    size_t copied = 0;
    for (fs::directory_iterator it(source.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto extension = lowercase(it->path().extension().u8string());
        auto filename = lowercase(it->path().filename().u8string());
        bool companion = lowercase(it->path().stem().u8string()) == wanted_stem &&
                         (extension == ".bdd" || extension == ".bdb" || extension == ".bddstudio");
        if (!companion && filename != wanted_meta) continue;
        fs::copy_file(it->path(), output / it->path().filename(), fs::copy_options::none, ec);
        if (ec) break;
        ++copied;
    }
    if (ec || !copied) {
        error = "Cannot copy the frozen stage companions: " + (ec ? ec.message() : std::string("none found"));
        std::error_code ignored; fs::remove_all(output, ignored); return false;
    }
    bdd_path = (output / source.filename()).u8string();
    return true;
}

bool library_audit_optimize_settings(const LibraryAuditRow &row, OptimizeOptions &options,
                                     int &engine_mode, std::string &error) {
    error.clear();
    if (row.best_mode != "preserve" && row.best_mode != "preserve_shared" &&
        row.best_mode != "palette" && row.best_mode != "palette_shared") {
        error = "The selected audit row has no supported verified optimization mode.";
        return false;
    }
    options = {};
    options.deep = true;
    options.compact_palettes = row.best_mode.rfind("palette", 0) == 0;
    options.max_added_objects = 24;
    options.source_image = -1;
    engine_mode = row.best_mode.find("_shared") != std::string::npos ? 1 : 0;
    return true;
}

} // namespace studio
