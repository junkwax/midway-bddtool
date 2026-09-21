#include "Core/studio_art_audit.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
namespace fs = std::filesystem;
std::string upper(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return s;
}
void check(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
std::string read(const fs::path &path, size_t limit) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    check(in && in.tellg() >= 0 && (uint64_t)in.tellg() <= limit,
          "Cannot read audit input (missing, inaccessible or too large): " + path.u8string());
    std::string text((size_t)in.tellg(), '\0');
    in.seekg(0);
    if (!text.empty()) in.read(text.data(), (std::streamsize)text.size());
    check((bool)in, "Could not finish reading " + path.u8string());
    return text;
}
bool symbol_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$' || c == '?' || c == '.' || c == '@';
}
// Small strict reader for the existing art_ref_graph.py JSON array. Unknown fields are
// parsed/skipped, never searched with regex. Strings, escapes and nested values stay isolated.
class GraphReader {
    const std::string &text;
    size_t pos = 0;
    void ws() { while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos; }
    char peek() { ws(); return pos < text.size() ? text[pos] : '\0'; }
    void take(char c) { check(peek() == c, "Malformed reference graph JSON."); ++pos; }
    unsigned hex4() {
        unsigned n = 0;
        for (int i = 0; i < 4; i++) {
            check(pos < text.size(), "Truncated JSON escape.");
            char c = text[pos++];
            int value = c >= '0' && c <= '9' ? c - '0' :
                        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                        c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            check(value >= 0, "Invalid JSON escape.");
            n = n * 16 + value;
        }
        return n;
    }
    std::string string() {
        take('"');
        std::string out;
        while (pos < text.size()) {
            unsigned char c = text[pos++];
            if (c == '"') return out;
            check(c >= 32, "Control character in JSON string.");
            if (c != '\\') { out += (char)c; continue; }
            check(pos < text.size(), "Truncated JSON escape.");
            c = text[pos++];
            switch (c) {
            case '"': case '\\': case '/': out += (char)c; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned n = hex4();
                if (n >= 0xd800 && n <= 0xdbff) {
                    check(pos + 2 <= text.size() && text.substr(pos, 2) == "\\u", "Missing JSON surrogate.");
                    pos += 2;
                    unsigned low = hex4();
                    check(low >= 0xdc00 && low <= 0xdfff, "Invalid JSON surrogate.");
                    n = 0x10000 + ((n - 0xd800) << 10) + low - 0xdc00;
                } else check(n < 0xdc00 || n > 0xdfff, "Unexpected JSON surrogate.");
                if (n < 0x80) out += (char)n;
                else if (n < 0x800) { out += (char)(0xc0 | (n >> 6)); out += (char)(0x80 | (n & 63)); }
                else {
                    if (n >= 0x10000) { out += (char)(0xf0 | (n >> 18)); out += (char)(0x80 | ((n >> 12) & 63)); }
                    else out += (char)(0xe0 | (n >> 12));
                    out += (char)(0x80 | ((n >> 6) & 63)); out += (char)(0x80 | (n & 63));
                }
                break;
            }
            default: throw std::runtime_error("Invalid JSON escape.");
            }
        }
        throw std::runtime_error("Unterminated JSON string.");
    }
    void value(int depth = 0) {
        check(depth < 32, "Reference graph JSON nesting limit exceeded.");
        char c = peek();
        if (c == '"') { string(); return; }
        if (c == '[' || c == '{') {
            ++pos;
            char end = c == '[' ? ']' : '}';
            if (peek() != end) for (;;) {
                if (c == '{') { string(); take(':'); }
                value(depth + 1);
                if (peek() == end) break;
                take(',');
            }
            take(end); return;
        }
        for (const char *literal : {"true", "false", "null"}) {
            std::string s = literal;
            if (text.compare(pos, s.size(), s) == 0) { pos += s.size(); return; }
        }
        if (c == '-') ++pos;
        check(pos < text.size() && std::isdigit((unsigned char)text[pos]), "Invalid JSON value.");
        if (text[pos] == '0') ++pos;
        else while (pos < text.size() && std::isdigit((unsigned char)text[pos])) ++pos;
        if (pos < text.size() && text[pos] == '.') {
            ++pos; size_t begin = pos;
            while (pos < text.size() && std::isdigit((unsigned char)text[pos])) ++pos;
            check(pos > begin, "Invalid JSON fraction.");
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
            size_t begin = pos;
            while (pos < text.size() && std::isdigit((unsigned char)text[pos])) ++pos;
            check(pos > begin, "Invalid JSON exponent.");
        }
    }
public:
    struct Row { std::string label, table, classification; };
    explicit GraphReader(const std::string &data) : text(data) {}
    std::vector<Row> parse() {
        std::vector<Row> rows;
        take('[');
        if (peek() != ']') for (;;) {
            take('{'); Row row; std::set<std::string> fields;
            if (peek() != '}') for (;;) {
                auto key = string(); take(':');
                check(fields.insert(key).second, "Duplicate reference graph field.");
                if (key == "label") row.label = string();
                else if (key == "table") row.table = string();
                else if (key == "class") row.classification = string();
                else value();
                if (peek() == '}') break;
                take(',');
            }
            take('}');
            check(!row.label.empty() && !row.table.empty() &&
                  (row.classification == "LIVE" || row.classification == "WINDOW" ||
                   row.classification == "AUTHORED" || row.classification == "DEAD"),
                  "Reference graph row has missing or unknown fields.");
            rows.push_back(std::move(row));
            check(rows.size() <= 100000, "Reference graph row limit exceeded.");
            if (peek() == ']') break;
            take(',');
        }
        take(']'); ws(); check(pos == text.size(), "Trailing reference graph data.");
        return rows;
    }
};
void evidence(ArtAuditEntry &entry, const std::string &text) {
    if (entry.evidence.size() < 24) entry.evidence.push_back(text);
    else if (entry.evidence.size() == 24) entry.evidence.push_back("Additional references omitted.");
}
} // namespace

ArtAudit audit_art(const Document &document, const std::string &root_path, OptimizeProgress *progress) {
    ArtAudit audit;
    audit.before = document.state();
    audit.root = root_path;
    try {
        auto checkpoint = [&]() {
            if (progress && progress->cancel) throw std::runtime_error("Art audit cancelled.");
        };
        checkpoint();
        const auto &state = audit.before;
        check(state.assets != nullptr, "Open artwork before auditing it.");
        std::map<std::string, std::vector<size_t>> labels;
        std::map<int, int> uses;
        for (const auto &p : state.objects) ++uses[p.object.ii];
        std::set<std::string> used_payloads, unplaced_payloads;
        auto payload_key = [](const BddCoreImage &im) {
            return std::to_string(im.w) + ":" + std::to_string(im.h) + ":" +
                   std::string((const char *)im.pix.data(), im.pix.size());
        };
        for (const auto &im : state.assets->data.images)
            if (uses[im.idx]) used_payloads.insert(payload_key(im));
        for (const auto &im : state.assets->data.images) {
            checkpoint();
            ArtAuditEntry entry;
            entry.id = im.idx;
            entry.placements = uses[im.idx];
            entry.estimated_bytes = (optimization_image_bits(im) + 7) / 8;
            entry.status = entry.placements ? "Placed" : "Unresolved";
            if (!entry.placements) {
                ++audit.unplaced_images;
                auto key = payload_key(im);
                if (!used_payloads.count(key) && unplaced_payloads.insert(key).second)
                    audit.unplaced_video_bytes += entry.estimated_bytes;
            }
            for (const auto &m : state.assets->metadata) if (m.idx == im.idx) {
                if (m.label[0]) entry.label = m.label;
                if (m.lod_ref || m.frm || m.opals || m.pttblnum || m.anix || m.aniy ||
                    m.anix2 || m.aniy2 || m.aniz2) {
                    if (!entry.placements) entry.status = "Runtime metadata";
                    evidence(entry, "Animation/LOD/anchor metadata: retain for consumer review.");
                }
                if (m.source[0]) evidence(entry, "Authored source: " + std::string(m.source));
            }
            if (!entry.label.empty()) labels[upper(entry.label)].push_back(audit.entries.size());
            else evidence(entry, "No assembly label mapping; external references cannot be resolved from image ID alone.");
            if (entry.placements) evidence(entry, std::to_string(entry.placements) + " placement(s), including hidden or locked objects.");
            audit.entries.push_back(std::move(entry));
        }
        for (size_t pi = 0; pi < state.assets->data.palettes.size(); pi++) {
            ArtAuditEntry entry;
            entry.palette = true; entry.id = (int)pi;
            const auto &pal = state.assets->data.palettes[pi];
            entry.label = pal.name;
            entry.estimated_bytes = pal.count * 2 + 2;
            for (const auto &p : state.objects) entry.placements += p.object.fl == (int)pi;
            for (int p : state.assets->default_palettes) entry.defaults += p == (int)pi;
            for (size_t other = 0; other < pi; other++) {
                const auto &q = state.assets->data.palettes[other];
                if (q.count == pal.count && std::equal(pal.rgb555, pal.rgb555 + pal.count, q.rgb555)) {
                    entry.duplicate_of = (int)other; break;
                }
            }
            if (!entry.placements) ++audit.unplaced_palettes;
            entry.status = entry.placements ? "Placed" : entry.defaults ? "Image default" : "Unresolved";
            if (entry.defaults) evidence(entry, std::to_string(entry.defaults) + " image default(s), including unplaced artwork.");
            if (entry.duplicate_of >= 0) evidence(entry, "RGB555 matches palette " + std::to_string(entry.duplicate_of) + "; slot identity/cycling may differ.");
            if (!entry.label.empty()) labels[upper(entry.label)].push_back(audit.entries.size());
            audit.entries.push_back(std::move(entry));
        }
        audit.notes.push_back("No deletion is authorized by this audit. Unplaced does not mean unused by the game.");
        audit.notes.push_back("Source mentions include definitions, not just consumers. Numeric references, computed addresses and runtime palette slots are not resolved.");
        audit.notes.push_back("Unplaced video bytes are an upper bound in this document, deduplicated against placed images; external sharing and build packing can reduce it.");
        if (root_path.empty()) {
            audit.notes.push_back("Choose the game checkout and scan again to collect source and graph evidence.");
            return audit;
        }
        auto root = fs::canonical(fs::u8path(root_path));
        audit.root = root.u8string();
        std::vector<fs::path> files;
        size_t total_bytes = 0;
        for (const auto &dir : {root / "src", root / "src-refactor" / "src", root / "data"}) {
            if (!fs::is_directory(dir)) continue;
            for (const auto &file : fs::recursive_directory_iterator(dir)) {
                checkpoint();
                if (file.is_symlink() || !file.is_regular_file()) continue;
                auto ext = upper(file.path().extension().u8string());
                if (ext != ".ASM" && ext != ".TBL" && ext != ".LOD" && ext != ".INC") continue;
                check(files.size() < 10000 && file.file_size() <= 16 * 1024 * 1024,
                      "Source scan limit exceeded; audit is incomplete.");
                total_bytes += (size_t)file.file_size();
                check(total_bytes <= 128 * 1024 * 1024, "Source scan size limit exceeded; audit is incomplete.");
                files.push_back(file.path());
            }
        }
        std::sort(files.begin(), files.end());
        if (progress) { progress->done = 0; progress->total = (int)files.size() + 1; }
        for (const auto &file : files) {
            checkpoint();
            std::istringstream lines(read(file, 16 * 1024 * 1024));
            std::string line; int number = 0;
            while (std::getline(lines, line)) {
                ++number;
                if (number % 256 == 0) checkpoint();
                auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '*') continue;
                bool quote = false; std::set<std::string> mentioned;
                for (size_t i = 0; i < line.size();) {
                    char c = line[i];
                    if (c == '"') { quote = !quote; ++i; continue; }
                    if (!quote && c == ';') break;
                    if (quote || !symbol_char((unsigned char)c)) { ++i; continue; }
                    size_t begin = i++;
                    while (i < line.size() && symbol_char((unsigned char)line[i])) ++i;
                    auto name = upper(line.substr(begin, i - begin));
                    if (!mentioned.insert(name).second) continue;
                    auto found = labels.find(name);
                    if (found == labels.end()) continue;
                    for (size_t index : found->second) {
                        auto &entry = audit.entries[index];
                        if (entry.status == "Unresolved") entry.status = "Source mention";
                        evidence(entry, fs::relative(file, root).u8string() + ":" + std::to_string(number) + "  " + line.substr(0, 180));
                    }
                }
            }
            ++audit.source_files;
            if (progress) ++progress->done;
        }
        auto graph = root / "tmp" / "art_ref_graph" / "current.json";
        if (fs::is_regular_file(graph)) {
            auto data = read(graph, 32 * 1024 * 1024);
            auto rows = GraphReader(data).parse();
            check(read(graph, 32 * 1024 * 1024) == data, "Reference graph changed during the audit; scan again.");
            audit.graph_path = graph.u8string();
            audit.notes.push_back("Graph evidence comes from existing current.json. Build freshness and label-to-BDD identity are unverified; DEAD is a review candidate, never proof of safe removal.");
            std::map<std::string, int> occurrences;
            for (const auto &row : rows) ++occurrences[upper(row.label)];
            for (const auto &row : rows) {
                checkpoint();
                auto found = labels.find(upper(row.label));
                if (found == labels.end()) continue;
                for (size_t index : found->second) {
                    auto &entry = audit.entries[index];
                    if (entry.palette) continue; // Graph nodes describe image headers, not palette slots.
                    if (entry.status == "Unresolved" || entry.status == "Source mention")
                        entry.status = "Graph evidence";
                    evidence(entry, "Graph label match: " + row.table + "/" + row.label + " = " + row.classification +
                              (occurrences[upper(row.label)] > 1 ? " (ambiguous label)" : "") + "; snapshot/identity unverified.");
                }
            }
        } else audit.notes.push_back("No existing tmp/art_ref_graph/current.json found; source evidence only.");
        if (files.empty()) audit.notes.push_back("No supported assembly/table/LOD/include files found; source coverage is incomplete.");
        if (progress) progress->done = progress->total.load();
    } catch (const std::exception &e) {
        audit.error = e.what();
        audit.cancelled = progress && progress->cancel;
    }
    return audit;
}
std::string art_audit_report(const ArtAudit &audit) {
    std::ostringstream out;
    out << "bddtool unused-art audit\nStage: " << audit.before.name << "\nCheckout: " << audit.root
        << "\nSource files: " << audit.source_files << "\nGraph: " << audit.graph_path
        << "\nUnplaced images: " << audit.unplaced_images << "; palettes without placements: "
        << audit.unplaced_palettes << "\nUnplaced video estimate (upper bound): " << audit.unplaced_video_bytes << " bytes\n";
    for (const auto &note : audit.notes) out << note << '\n';
    if (!audit.error.empty()) out << "INCOMPLETE: " << audit.error << '\n';
    for (const auto &entry : audit.entries) {
        out << '\n' << (entry.palette ? "Palette " : "Image ") << entry.id << ' ' << entry.label
            << " | " << entry.status << " | placements " << entry.placements
            << " | standalone estimate " << entry.estimated_bytes << " bytes\n";
        for (const auto &reference : entry.evidence) out << "  " << reference << '\n';
    }
    return out.str();
}
} // namespace studio
