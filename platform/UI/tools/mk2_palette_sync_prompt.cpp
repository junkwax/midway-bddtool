#include "bg_editor_globals.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define mk2_sync_strcasecmp _stricmp
#else
#include <strings.h>
#define mk2_sync_strcasecmp strcasecmp
#endif

static unsigned short mk2_sync_rgb555_from_rgba(int r, int g, int b)
{
    int r5 = (r * 31 + 127) / 255;
    int g5 = (g * 31 + 127) / 255;
    int b5 = (b * 31 + 127) / 255;
    if (r5 < 0) r5 = 0;
    if (g5 < 0) g5 = 0;
    if (b5 < 0) b5 = 0;
    if (r5 > 31) r5 = 31;
    if (g5 > 31) g5 = 31;
    if (b5 > 31) b5 = 31;
    return (unsigned short)((r5 << 10) | (g5 << 5) | b5);
}

static void mk2_sync_uppercase_ascii_inplace(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
}

static void mk2_sync_stage_basename_no_ext(const char *path, char *out, size_t outsz)
{
    const char *base = path ? path : "";
    for (const char *p = base; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    snprintf(out, outsz, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}
static std::string mk2_trim_copy(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        b--;
    return s.substr(a, b - a);
}

static bool mk2_string_eq_ci(const std::string &a, const std::string &b)
{
    return mk2_sync_strcasecmp(a.c_str(), b.c_str()) == 0;
}

static bool mk2_label_name_from_line(const std::string &line, std::string *out)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= line.size()) return false;
    unsigned char ch = (unsigned char)line[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_'))
        return false;
    size_t start = i++;
    while (i < line.size()) {
        ch = (unsigned char)line[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
            break;
        i++;
    }
    if (i < line.size() && line[i] == ':') {
        if (out) *out = line.substr(start, i - start);
        return true;
    }
    return false;
}

static int mk2_find_label_line(const std::vector<std::string> &lines, const char *label)
{
    if (!label || !label[0]) return -1;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string found;
        if (mk2_label_name_from_line(lines[i], &found) && mk2_sync_strcasecmp(found.c_str(), label) == 0)
            return (int)i;
    }
    return -1;
}

static int mk2_section_end(const std::vector<std::string> &lines, int start)
{
    if (start < 0) return (int)lines.size();
    for (int i = start + 1; i < (int)lines.size(); i++)
        if (mk2_label_name_from_line(lines[(size_t)i], NULL))
            return i;
    return (int)lines.size();
}

static bool mk2_read_text_lines(const char *path, std::vector<std::string> &lines)
{
    lines.clear();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[2048];
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
        lines.push_back(buf);
    }
    fclose(f);
    return true;
}

static bool mk2_write_text_lines(const char *path, const std::vector<std::string> &lines)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = true;
    for (size_t i = 0; i < lines.size(); i++) {
        if (fputs(lines[i].c_str(), f) < 0 || fputc('\n', f) == EOF) {
            ok = false;
            break;
        }
    }
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool mk2_copy_file_unique(const char *src, const char *suffix, char *backup_out, size_t backup_outsz)
{
    if (backup_out && backup_outsz) backup_out[0] = '\0';
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    char dst[640];
    for (int i = 0; i < 100; i++) {
        if (i == 0) snprintf(dst, sizeof dst, "%s%s", src, suffix);
        else snprintf(dst, sizeof dst, "%s%s.%d", src, suffix, i);
        if (!stage_file_exists(dst)) break;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    bool ok = true;
    char buf[8192];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) {
        remove(dst);
        return false;
    }
    if (backup_out && backup_outsz) snprintf(backup_out, backup_outsz, "%s", dst);
    return true;
}

static unsigned short mk2_palette_word_from_argb(Uint32 c)
{
    unsigned char r = (unsigned char)((c >> 16) & 0xFF);
    unsigned char g = (unsigned char)((c >> 8) & 0xFF);
    unsigned char b = (unsigned char)(c & 0xFF);
    return mk2_sync_rgb555_from_rgba(r, g, b);
}

static std::string mk2_asm_hex(unsigned short value)
{
    char text[16];
    snprintf(text, sizeof text, "%X", value & 0xFFFF);
    std::string s = text;
    if (s.empty() || s == "0") return "00H";
    char c = s[0];
    if (c >= 'A' && c <= 'F') s.insert(s.begin(), '0');
    s += "H";
    return s;
}

static std::string mk2_sanitize_asm_label(const char *name, std::vector<std::string> &used)
{
    std::string label;
    const char *src = (name && name[0]) ? name : "PAL";
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char ch = *p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_')
            label.push_back((char)ch);
        else
            label.push_back('_');
    }
    if (label.empty()) label = "PAL";
    unsigned char first = (unsigned char)label[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
        label.insert(label.begin(), '_');
    if (label.size() > 60) label.resize(60);
    std::string base = label;
    int suffix = 2;
    bool unique = false;
    while (!unique) {
        unique = true;
        for (const std::string &u : used) {
            if (mk2_string_eq_ci(u, label)) {
                unique = false;
                break;
            }
        }
        if (!unique) {
            char tail[16];
            snprintf(tail, sizeof tail, "_%d", suffix++);
            label = base;
            if (label.size() + strlen(tail) > 60)
                label.resize(60 - strlen(tail));
            label += tail;
        }
    }
    used.push_back(label);
    return label;
}

static std::vector<std::string> mk2_palette_block_lines(const std::string &label, int pal_index)
{
    int n = g_pal_count[pal_index];
    if (n < 0) n = 0;
    if (n > 256) n = 256;

    /* Prefer the exact RGB555 words captured from the BDD so the runtime table
       reproduces the stored palette verbatim, including a non-black index 0
       (which g_pals force-zeroes for display transparency). The accessor only
       succeeds when the palette is unchanged since load; edited palettes fall
       back to re-deriving the word from the displayed ARGB color. */
    Uint16 raw[256];
    int raw_count = editor_project_get_palette_rgb555_cache(pal_index, raw, 256);

    std::vector<std::string> block;
    char line[256];
    snprintf(line, sizeof line, "%s:\t;PAL #%d", label.c_str(), pal_index);
    block.push_back(line);
    snprintf(line, sizeof line, "\t.word\t%d\t;pal size", n);
    block.push_back(line);
    for (int start = 0; start < n; start += 10) {
        std::string row = "\t.word ";
        int end = start + 10;
        if (end > n) end = n;
        for (int i = start; i < end; i++) {
            if (i > start) row += ",";
            unsigned short word = (i < raw_count)
                ? raw[i]
                : mk2_palette_word_from_argb(g_pals[pal_index][i]);
            row += mk2_asm_hex(word);
        }
        block.push_back(row);
    }
    block.push_back("");
    return block;
}

static bool mk2_replace_or_insert_palette_block(std::vector<std::string> &lines,
                                                const std::string &label,
                                                int pal_index,
                                                int *insert_before)
{
    std::vector<std::string> block = mk2_palette_block_lines(label, pal_index);
    int existing = mk2_find_label_line(lines, label.c_str());
    if (existing >= 0) {
        int end = mk2_section_end(lines, existing);
        bool same = ((int)block.size() == end - existing);
        if (same) {
            for (int i = existing; i < end; i++) {
                if (lines[(size_t)i] != block[(size_t)(i - existing)]) {
                    same = false;
                    break;
                }
            }
        }
        if (same) return false;
        lines.erase(lines.begin() + existing, lines.begin() + end);
        lines.insert(lines.begin() + existing, block.begin(), block.end());
        int delta = (int)block.size() - (end - existing);
        if (insert_before && existing < *insert_before)
            *insert_before += delta;
        return true;
    }

    int at = insert_before ? *insert_before : (int)lines.size();
    if (at < 0 || at > (int)lines.size()) at = (int)lines.size();
    lines.insert(lines.begin() + at, block.begin(), block.end());
    if (insert_before) *insert_before = at + (int)block.size();
    return true;
}

static void mk2_palette_sync_remove_stale_outputs(const char *bgnpal, int *removed);

/* Background-palette region budget. The pristine shipped BGNDPAL assembles to
   ~18.8 KB; its .DATA links into the shared ROM region ahead of the address-
   fixed REVX/MK8MIL movie space, which has no headroom. We flag well before
   that: this soft cap leaves room for a handful of genuinely new stage palettes
   but catches runaway duplication (the failure that overran ARMORY). */
#define MK2_BGNDPAL_BUDGET_BYTES 32768

static long mk2_asm_word_value(const std::string &tok)
{
    std::string t = mk2_trim_copy(tok);
    if (t.empty()) return -1;
    bool hex = false;
    if (t.back() == 'H' || t.back() == 'h') { hex = true; t.pop_back(); }
    if (t.empty()) return -1;
    char *endp = nullptr;
    long v = strtol(t.c_str(), &endp, hex ? 16 : 10);
    if (endp == t.c_str()) return -1;
    return v & 0xFFFF;
}

/* Assembled data size of BGNDPAL.ASM: each .word value = 2 bytes, .long = 4.
   The whole file is .DATA, so this equals the BGNDPAL.OBJ footprint competing
   for ROM space ahead of the reserved REVX/MK8MIL region. */
static long mk2_bgndpal_assembled_bytes(const std::vector<std::string> &lines)
{
    long bytes = 0;
    for (const std::string &raw : lines) {
        std::string line = raw;
        size_t semi = line.find(';');
        if (semi != std::string::npos) line.resize(semi);
        int unit = 0;
        size_t p = line.find(".word");
        if (p != std::string::npos) unit = 2;
        else if ((p = line.find(".long")) != std::string::npos) unit = 4;
        else continue;
        std::string rest = line.substr(p + 5);
        size_t start = 0;
        while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string one = mk2_trim_copy(rest.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!one.empty()) bytes += unit;
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return bytes;
}

/* Numeric .word values in a block body (start+1 .. end). A palette data block
   yields its size word + colour words; a *PALS table (.long only) yields an
   empty vector, which we treat as "not a palette data block". */
static std::vector<long> mk2_block_words(const std::vector<std::string> &lines, int start, int end)
{
    std::vector<long> w;
    for (int i = start + 1; i < end; i++) {
        std::string line = lines[(size_t)i];
        size_t semi = line.find(';');
        if (semi != std::string::npos) line.resize(semi);
        size_t p = line.find(".word");
        if (p == std::string::npos) continue;
        std::string rest = line.substr(p + 5);
        size_t s = 0;
        while (s <= rest.size()) {
            size_t comma = rest.find(',', s);
            long v = mk2_asm_word_value(rest.substr(
                s, comma == std::string::npos ? std::string::npos : comma - s));
            if (v >= 0) w.push_back(v);
            if (comma == std::string::npos) break;
            s = comma + 1;
        }
    }
    return w;
}

/* The words a freshly-built palette block would assemble to (size + colours),
   matching mk2_palette_block_lines so the two can be compared. */
static std::vector<long> mk2_palette_words(int pal_index)
{
    int n = g_pal_count[pal_index];
    if (n < 0) n = 0;
    if (n > 256) n = 256;
    Uint16 raw[256];
    int raw_count = editor_project_get_palette_rgb555_cache(pal_index, raw, 256);
    std::vector<long> w;
    w.push_back(n);
    for (int i = 0; i < n; i++) {
        unsigned short word = (i < raw_count) ? raw[i]
                              : mk2_palette_word_from_argb(g_pals[pal_index][i]);
        w.push_back((long)word);
    }
    return w;
}

/* Find an existing palette data block whose colour words match `words`, so sync
   can reuse it instead of writing a duplicate. Returns its label, or empty. */
static std::string mk2_find_matching_data_block(const std::vector<std::string> &lines,
                                                const std::vector<long> &words)
{
    if (words.empty()) return std::string();
    for (size_t i = 0; i < lines.size(); i++) {
        std::string name;
        if (!mk2_label_name_from_line(lines[i], &name)) continue;
        int end = mk2_section_end(lines, (int)i);
        if (mk2_block_words(lines, (int)i, end) == words)
            return name;
    }
    return std::string();
}

/* Rewrite a ".long a,b,c" reference line, replacing any operand listed in
   `rename` (dup -> canonical), preserving indentation and trailing comment. */
static std::string mk2_repoint_long_line(const std::string &line,
                                         const std::vector<std::pair<std::string,std::string>> &rename)
{
    size_t p = line.find(".long");
    if (p == std::string::npos) return line;
    std::string head = line.substr(0, p + 5);
    std::string rest = line.substr(p + 5);
    std::string comment;
    size_t semi = rest.find(';');
    if (semi != std::string::npos) { comment = rest.substr(semi); rest.resize(semi); }

    std::string out = head;
    size_t start = 0;
    bool first = true;
    bool replaced = false;
    while (start <= rest.size()) {
        size_t comma = rest.find(',', start);
        std::string tok = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::string trimmed = mk2_trim_copy(tok);
        for (const auto &r : rename) {
            if (mk2_string_eq_ci(trimmed, r.first)) { trimmed = r.second; replaced = true; break; }
        }
        out += first ? (std::string("\t") + trimmed) : (std::string(",") + trimmed);
        first = false;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (!comment.empty()) out += comment;
    return replaced ? out : line;
}

/* Collapse content-identical palette blocks: keep the first label per unique
   palette, remove the duplicates, and repoint every *PALS .long reference to
   the survivor. Returns blocks removed, 0 if none, -1 on error. */
int mk2_bgndpal_compact(const char *bgnpal, char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    if (!bgnpal || !bgnpal[0] || !stage_file_exists(bgnpal)) {
        if (status) snprintf(status, statussz, "BGNDPAL.ASM path is missing.");
        return -1;
    }
    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnpal, lines)) {
        if (status) snprintf(status, statussz, "Could not read %s", bgnpal);
        return -1;
    }

    struct Blk { std::string label; int start; std::vector<long> words; };
    std::vector<Blk> blocks;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string name;
        if (!mk2_label_name_from_line(lines[i], &name)) continue;
        int end = mk2_section_end(lines, (int)i);
        std::vector<long> w = mk2_block_words(lines, (int)i, end);
        if (w.empty()) continue;
        blocks.push_back({name, (int)i, w});
    }

    std::vector<std::pair<std::string,std::string>> rename;  /* dup -> canonical */
    std::vector<int> remove_starts;
    for (size_t a = 0; a < blocks.size(); a++) {
        for (size_t b = 0; b < a; b++) {
            if (blocks[b].words == blocks[a].words &&
                !mk2_string_eq_ci(blocks[b].label, blocks[a].label)) {
                rename.push_back({blocks[a].label, blocks[b].label});
                remove_starts.push_back(blocks[a].start);
                break;
            }
        }
    }
    if (rename.empty()) {
        if (status) snprintf(status, statussz, "No duplicate palette blocks found.");
        return 0;
    }

    for (std::string &line : lines)
        if (line.find(".long") != std::string::npos)
            line = mk2_repoint_long_line(line, rename);

    std::sort(remove_starts.begin(), remove_starts.end(), std::greater<int>());
    for (int s : remove_starts) {
        int e = mk2_section_end(lines, s);
        lines.erase(lines.begin() + s, lines.begin() + e);
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnpal, ".pre_bgndpal_compact", backup, sizeof backup)) {
        if (status) snprintf(status, statussz, "Could not back up BGNDPAL.ASM.");
        return -1;
    }
    if (!mk2_write_text_lines(bgnpal, lines)) {
        if (status) snprintf(status, statussz, "Could not write BGNDPAL.ASM; backup: %s", backup);
        return -1;
    }
    mk2_palette_sync_remove_stale_outputs(bgnpal, NULL);
    if (status)
        snprintf(status, statussz, "Removed %d duplicate palette block(s). Backup: %s",
                 (int)rename.size(), backup);
    return (int)rename.size();
}

static bool mk2_palette_sync_find_bgnpal_path(char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (g_runtime_palette_asm[0] && stage_file_exists(g_runtime_palette_asm)) {
        snprintf(out, outsz, "%s", g_runtime_palette_asm);
        return true;
    }
    char path[640];
    if (g_stage_mk2_root[0]) {
        path_join(path, sizeof path, g_stage_mk2_root, "src\\BGNDPAL.ASM");
        if (stage_file_exists(path)) {
            snprintf(out, outsz, "%s", path);
            snprintf(g_runtime_palette_asm, sizeof g_runtime_palette_asm, "%s", path);
            return true;
        }
    }
    const char *project_path = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (project_path && project_path[0]) {
        char dir[512];
        stage_dirname(project_path, dir, sizeof dir);
        path_join(path, sizeof path, dir, "..\\src\\BGNDPAL.ASM");
        if (stage_file_exists(path)) {
            snprintf(out, outsz, "%s", path);
            snprintf(g_runtime_palette_asm, sizeof g_runtime_palette_asm, "%s", path);
            return true;
        }
    }
    return false;
}

static bool mk2_palette_sync_bgndtbl_for_bgnpal(const char *bgnpal, char *out, size_t outsz)
{
    if (!bgnpal || !bgnpal[0]) return false;
    char dir[512];
    stage_dirname(bgnpal, dir, sizeof dir);
    path_join(out, outsz, dir, "BGNDTBL.ASM");
    return stage_file_exists(out);
}

static void mk2_palette_sync_stage_key(char *out, size_t outsz)
{
    out[0] = '\0';
    char nm[64] = "";
    int ww, wh, md, nmodes, np, no;
    if (g_bdb_header[0] &&
        sscanf(g_bdb_header, "%63s %d %d %d %d %d %d", nm, &ww, &wh, &md, &nmodes, &np, &no) >= 1)
        snprintf(out, outsz, "%s", nm);
    else if (g_name[0])
        snprintf(out, outsz, "%s", g_name);
    else if (g_bdb_path[0] || g_bdd_path[0])
        mk2_sync_stage_basename_no_ext(g_bdb_path[0] ? g_bdb_path : g_bdd_path, out, outsz);
    mk2_sync_uppercase_ascii_inplace(out);
}

static bool mk2_palette_sync_infer_table_from_bgndtbl(const char *bgnpal, char *out, size_t outsz)
{
    out[0] = '\0';
    char tbl[640];
    if (!mk2_palette_sync_bgndtbl_for_bgnpal(bgnpal, tbl, sizeof tbl))
        return false;

    std::vector<std::string> wanted;
    for (int i = 0; i < g_bdb_num_modules; i++) {
        char mod[64] = "";
        if (sscanf(g_bdb_modules[i], "%63s", mod) == 1 && mod[0]) {
            std::string w = mod;
            w += "BLKS";
            wanted.push_back(w);
        }
    }
    if (wanted.empty()) return false;

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(tbl, lines)) return false;
    for (const std::string &line_raw : lines) {
        std::string line = line_raw;
        size_t semi = line.find(';');
        if (semi != std::string::npos) line.resize(semi);
        std::string lower = line;
        for (char &c : lower)
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        size_t p = lower.find(".long");
        if (p == std::string::npos) continue;
        std::string rest = line.substr(p + 5);
        std::vector<std::string> tok;
        size_t start = 0;
        while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string one = mk2_trim_copy(rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!one.empty()) tok.push_back(one);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (tok.size() < 3) continue;
        for (const std::string &w : wanted) {
            if (mk2_string_eq_ci(tok[0], w)) {
                snprintf(out, outsz, "%s", tok[2].c_str());
                return true;
            }
        }
    }
    return false;
}

static bool mk2_palette_sync_infer_table(const char *bgnpal, char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (mk2_palette_sync_infer_table_from_bgndtbl(bgnpal, out, outsz))
        return true;

    char key[96];
    mk2_palette_sync_stage_key(key, sizeof key);
    if (strstr(key, "FOREST2") || strstr(key, "WOOD")) {
        snprintf(out, outsz, "st2PALS");
        return true;
    }
    if (strstr(key, "BATTLE")) {
        snprintf(out, outsz, "lePALS");
        return true;
    }
    if (strstr(key, "BGPROF") || strstr(key, "OUTER")) {
        snprintf(out, outsz, "OFPALS");
        return true;
    }
    return false;
}

static void mk2_palette_sync_remove_stale_outputs(const char *bgnpal, int *removed)
{
    if (removed) *removed = 0;
    char dir[512];
    stage_dirname(bgnpal, dir, sizeof dir);
    const char *rel[] = {
        "BGND.OBJ", "BGND.LST", "BGNDPAL.OBJ", "BGNDPAL.LST",
        "BGNDTBL.OBJ", "BGNDTBL.LST", "mk2.out"
    };
    for (int i = 0; i < (int)(sizeof rel / sizeof rel[0]); i++) {
        char path[640];
        path_join(path, sizeof path, dir, rel[i]);
        if (stage_file_exists(path) && remove(path) == 0 && removed)
            (*removed)++;
    }
}

/* BGNDPAL.ASM is shared by every stage. Collect the labels that do NOT belong
   to the target palette table (foreign blocks), plus the table label itself, so
   we can seed the label-dedup set with them. A BDD palette whose name collides
   with a foreign block then gets renamed and written as a fresh block instead of
   silently overwriting another stage's palette. Labels the current table already
   references stay reusable, so re-syncing keeps replacing our own blocks. */
static void mk2_collect_foreign_labels(const std::vector<std::string> &lines,
                                       const char *table_label,
                                       std::vector<std::string> &out_foreign)
{
    out_foreign.clear();

    std::vector<std::string> owned;
    int table_start = mk2_find_label_line(lines, table_label);
    if (table_start >= 0) {
        int table_end = mk2_section_end(lines, table_start);
        for (int i = table_start + 1; i < table_end; i++) {
            std::string line = lines[(size_t)i];
            size_t semi = line.find(';');
            if (semi != std::string::npos) line.resize(semi);
            size_t p = line.find(".long");
            if (p == std::string::npos) continue;
            std::string rest = line.substr(p + 5);
            size_t start = 0;
            while (start <= rest.size()) {
                size_t comma = rest.find(',', start);
                std::string one = mk2_trim_copy(rest.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start));
                if (!one.empty()) owned.push_back(one);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }

    auto is_owned = [&](const std::string &name) {
        if (table_label && mk2_sync_strcasecmp(name.c_str(), table_label) == 0)
            return true;
        for (const std::string &o : owned)
            if (mk2_string_eq_ci(o, name)) return true;
        return false;
    };
    auto already_listed = [&](const std::string &name) {
        for (const std::string &f : out_foreign)
            if (mk2_string_eq_ci(f, name)) return true;
        return false;
    };

    for (const std::string &raw : lines) {
        std::string name;
        if (!mk2_label_name_from_line(raw, &name)) continue;
        if (is_owned(name) || already_listed(name)) continue;
        out_foreign.push_back(name);
    }

    /* Never let a palette reuse the table label itself. */
    if (table_label && table_label[0] && !already_listed(table_label))
        out_foreign.push_back(table_label);
}

static bool mk2_palette_sync_current_matches(const char *bgnpal, const char *table_label,
                                             char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    if (!bgnpal || !bgnpal[0] || !stage_file_exists(bgnpal)) return false;
    if (!table_label || !table_label[0] || g_n_pals <= 0) return false;

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnpal, lines)) return false;

    /* Already synced iff every current palette already has an identical block
       (so sync would reuse it) and the table lists exactly those labels. */
    std::vector<std::string> labels((size_t)g_n_pals);
    for (int i = 0; i < g_n_pals; i++) {
        std::string match = mk2_find_matching_data_block(lines, mk2_palette_words(i));
        if (match.empty()) return false;
        labels[(size_t)i] = match;
    }

    int table_start = mk2_find_label_line(lines, table_label);
    if (table_start < 0) return false;
    int table_end = mk2_section_end(lines, table_start);
    std::vector<std::string> table;
    table.push_back(std::string(table_label) + ":");
    for (const std::string &label : labels)
        table.push_back(std::string("\t.long\t") + label);
    if ((int)table.size() != table_end - table_start) return false;
    for (int i = 0; i < (int)table.size(); i++)
        if (lines[(size_t)(table_start + i)] != table[(size_t)i])
            return false;

    if (status && statussz)
        snprintf(status, statussz, "Runtime palette table is already synced.");
    return true;
}

static bool mk2_palette_sync_apply(const char *bgnpal, const char *table_label,
                                   char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    g_mk2_palette_sync_output[0] = '\0';
    if (!bgnpal || !bgnpal[0] || !stage_file_exists(bgnpal)) {
        snprintf(status, statussz, "BGNDPAL.ASM path is missing.");
        return false;
    }
    if (!table_label || !table_label[0]) {
        snprintf(status, statussz, "Palette table label is missing.");
        return false;
    }
    if (g_n_pals <= 0) {
        snprintf(status, statussz, "Current BDD has no palettes to sync.");
        return false;
    }

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnpal, lines)) {
        snprintf(status, statussz, "Could not read %s", bgnpal);
        return false;
    }
    int table_start = mk2_find_label_line(lines, table_label);
    if (table_start < 0) {
        snprintf(status, statussz, "Palette table %s was not found.", table_label);
        return false;
    }

    std::vector<std::string> used;
    mk2_collect_foreign_labels(lines, table_label, used);

    bool changed = false;
    std::vector<std::string> labels((size_t)g_n_pals);
    for (int i = 0; i < g_n_pals; i++) {
        /* Reuse an existing block with identical colours instead of writing a
           duplicate; only mint a new label when the palette is genuinely new.
           This is what keeps BGNDPAL.ASM from ballooning across stages. */
        std::string match = mk2_find_matching_data_block(lines, mk2_palette_words(i));
        if (!match.empty()) {
            labels[(size_t)i] = match;
            continue;
        }
        std::string lbl = mk2_sanitize_asm_label(g_pal_name[i], used);
        labels[(size_t)i] = lbl;
        int ins = mk2_find_label_line(lines, table_label);
        changed = mk2_replace_or_insert_palette_block(lines, lbl, i, &ins) || changed;
    }

    table_start = mk2_find_label_line(lines, table_label);
    int table_end = mk2_section_end(lines, table_start);
    std::vector<std::string> table;
    std::string label_line = std::string(table_label) + ":";
    table.push_back(label_line);
    for (const std::string &label : labels)
        table.push_back(std::string("\t.long\t") + label);
    bool same_table = ((int)table.size() == table_end - table_start);
    if (same_table) {
        for (int i = table_start; i < table_end; i++) {
            if (lines[(size_t)i] != table[(size_t)(i - table_start)]) {
                same_table = false;
                break;
            }
        }
    }
    if (!same_table) {
        lines.erase(lines.begin() + table_start, lines.begin() + table_end);
        lines.insert(lines.begin() + table_start, table.begin(), table.end());
        changed = true;
    }

    if (!changed) {
        snprintf(status, statussz, "Runtime palette table is already synced.");
        snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
                 "%s already matches %d current BDD palette(s).", table_label, g_n_pals);
        g_mk2_palette_sync_dirty = false;
        return true;
    }

    long assembled = mk2_bgndpal_assembled_bytes(lines);
    if (assembled > MK2_BGNDPAL_BUDGET_BYTES && !g_mk2_palette_allow_over_budget) {
        snprintf(status, statussz,
                 "Refused: BGNDPAL.ASM would assemble to %ld bytes (budget %d). This "
                 "risks overrunning the reserved REVX/MK8MIL ROM space. Run \"Compact "
                 "duplicates\" first, or enable the over-budget override.",
                 assembled, MK2_BGNDPAL_BUDGET_BYTES);
        snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
                 "Not written. Assembled palette data %ld bytes exceeds the %d-byte budget.",
                 assembled, MK2_BGNDPAL_BUDGET_BYTES);
        return false;
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnpal, ".pre_bdd_palette_sync", backup, sizeof backup)) {
        snprintf(status, statussz, "Could not create backup for BGNDPAL.ASM.");
        return false;
    }
    if (!mk2_write_text_lines(bgnpal, lines)) {
        snprintf(status, statussz, "Could not write BGNDPAL.ASM; backup: %s", backup);
        return false;
    }
    int removed = 0;
    mk2_palette_sync_remove_stale_outputs(bgnpal, &removed);
    snprintf(status, statussz, "Synced %d palette(s) into %s.", g_n_pals, table_label);
    snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
             "Updated %s\nBackup: %s\nRemoved %d stale assembler product(s).",
             bgnpal, backup, removed);
    g_mk2_palette_sync_dirty = false;
    return true;
}

bool mk2_palette_sync_auto_apply_if_ready(const char *reason)
{
    char path[512] = "";
    if (!mk2_palette_sync_find_bgnpal_path(path, sizeof path))
        return false;
    char table[64] = "";
    if (!mk2_palette_sync_infer_table(path, table, sizeof table))
        return false;
    snprintf(g_mk2_palette_sync_asm, sizeof g_mk2_palette_sync_asm, "%s", path);
    snprintf(g_mk2_palette_sync_table, sizeof g_mk2_palette_sync_table, "%s", table);
    snprintf(g_mk2_palette_sync_reason, sizeof g_mk2_palette_sync_reason,
             "%s", reason ? reason : "BDD palettes changed");
    bool ok = mk2_palette_sync_apply(path, table,
                                     g_mk2_palette_sync_status,
                                     sizeof g_mk2_palette_sync_status);
    g_mk2_palette_sync_last_rc = ok ? 0 : 1;
    stage_set_toast(ok ? "Runtime palettes promoted" : "Runtime palette promotion failed");
    return ok;
}

void mk2_palette_sync_request_prompt(const char *reason, bool allow_if_unknown_path)
{
    if (g_n_pals <= 0) return;
    char path[512] = "";
    bool has_path = mk2_palette_sync_find_bgnpal_path(path, sizeof path);
    if (!has_path && !allow_if_unknown_path)
        return;
    if (has_path)
        snprintf(g_mk2_palette_sync_asm, sizeof g_mk2_palette_sync_asm, "%s", path);
    if (has_path) {
        char inferred[64] = "";
        if (mk2_palette_sync_infer_table(path, inferred, sizeof inferred))
            snprintf(g_mk2_palette_sync_table, sizeof g_mk2_palette_sync_table, "%s", inferred);
    }
    if (has_path && g_mk2_palette_sync_table[0] &&
        mk2_palette_sync_current_matches(path, g_mk2_palette_sync_table,
                                         g_mk2_palette_sync_status,
                                         sizeof g_mk2_palette_sync_status)) {
        g_mk2_palette_sync_dirty = false;
        snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
                 "%s already matches %d current BDD palette(s).",
                 g_mk2_palette_sync_table, g_n_pals);
        stage_set_toast("Runtime palettes already synced");
        return;
    }
    snprintf(g_mk2_palette_sync_reason, sizeof g_mk2_palette_sync_reason, "%s", reason ? reason : "BDD palettes changed");
    if (has_path && !g_mk2_palette_sync_table[0])
        snprintf(g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status,
                 "Found BGNDPAL.ASM, but could not infer the table label. Enter the *PALS label.");
    else if (has_path)
        snprintf(g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status,
                 "Ready to sync %d BDD palette(s) into %s.", g_n_pals, g_mk2_palette_sync_table);
    else
        snprintf(g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status,
                 "Choose BGNDPAL.ASM and the target *PALS label.");
    g_mk2_palette_sync_popup = true;
}

void draw_mk2_palette_sync_prompt(void)
{
    if (g_mk2_palette_sync_popup) {
        ImGui::OpenPopup("Sync MK2 Runtime Palettes");
        g_mk2_palette_sync_popup = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Sync MK2 Runtime Palettes", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("The BDD palettes are saved in the BDD file. MK2 also needs its runtime palette table in BGNDPAL.ASM updated before packaging.");
        if (g_mk2_palette_sync_reason[0])
            ImGui::TextDisabled("%s", g_mk2_palette_sync_reason);
        ImGui::Separator();
        draw_path_field("BGNDPAL.ASM", g_mk2_palette_sync_asm, sizeof g_mk2_palette_sync_asm,
                        "Select BGNDPAL.ASM", "ASM Files\0*.ASM;*.asm\0All Files\0*.*\0");
        ImGui::InputText("Palette Table", g_mk2_palette_sync_table, sizeof g_mk2_palette_sync_table);
        ImGui::SameLine();
        if (ImGui::SmallButton("Infer##runtime_pals")) {
            char inferred[64] = "";
            if (mk2_palette_sync_infer_table(g_mk2_palette_sync_asm, inferred, sizeof inferred)) {
                snprintf(g_mk2_palette_sync_table, sizeof g_mk2_palette_sync_table, "%s", inferred);
                snprintf(g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status,
                         "Inferred palette table %s.", inferred);
            } else {
                snprintf(g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status,
                         "Could not infer a *PALS table from BGNDTBL.ASM.");
            }
        }
        ImGui::Checkbox("Prompt after IMG imports", &g_mk2_palette_prompt_after_img_import);
        ImGui::Checkbox("Prompt after Save", &g_mk2_palette_prompt_after_save);
        ImGui::Checkbox("Auto-promote on Save when inferred", &g_mk2_palette_auto_sync_on_save);

        /* ROM budget readout + duplicate cleanup. */
        if (g_mk2_palette_sync_asm[0] && stage_file_exists(g_mk2_palette_sync_asm)) {
            std::vector<std::string> blines;
            if (mk2_read_text_lines(g_mk2_palette_sync_asm, blines)) {
                long bytes = mk2_bgndpal_assembled_bytes(blines);
                bool over = bytes > MK2_BGNDPAL_BUDGET_BYTES;
                ImGui::TextColored(over ? ImVec4(1.0f, 0.45f, 0.30f, 1.0f)
                                        : ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                                   "Palette ROM use: %.1f / %.1f KB%s",
                                   bytes / 1024.0, MK2_BGNDPAL_BUDGET_BYTES / 1024.0,
                                   over ? "   OVER BUDGET" : "");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("BGNDPAL.ASM links ahead of the reserved REVX/MK8MIL ROM\n"
                                      "space. Duplicate palette blocks across stages bloat this\n"
                                      "and overrun that region (the failure that broke ARMORY).");
                if (ImGui::Button("Compact duplicates", ImVec2(160, 0))) {
                    int n = mk2_bgndpal_compact(g_mk2_palette_sync_asm,
                                                g_mk2_palette_sync_status,
                                                sizeof g_mk2_palette_sync_status);
                    stage_set_toast(n > 0 ? "Compacted duplicate palette blocks"
                                          : (n == 0 ? "No duplicate palette blocks"
                                                    : "Compact failed"));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove palette blocks with identical colours and repoint every\n"
                                      "*PALS table to the survivor. Makes a .pre_bgndpal_compact backup.");
                if (over) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Override budget", &g_mk2_palette_allow_over_budget);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Write past the budget anyway. Only if you know the ROM has room.");
                }
            }
        }

        if (g_mk2_palette_sync_status[0])
            ImGui::TextWrapped("%s", g_mk2_palette_sync_status);
        if (g_mk2_palette_sync_output[0])
            ImGui::InputTextMultiline("Last Sync", g_mk2_palette_sync_output,
                                      sizeof g_mk2_palette_sync_output,
                                      ImVec2(520, 86), ImGuiInputTextFlags_ReadOnly);
        ImGui::Separator();
        bool can_sync = g_mk2_palette_sync_asm[0] && g_mk2_palette_sync_table[0] && g_n_pals > 0;
        if (!can_sync) ImGui::BeginDisabled();
        if (ImGui::Button("Sync Now", ImVec2(120, 0))) {
            bool ok = mk2_palette_sync_apply(g_mk2_palette_sync_asm, g_mk2_palette_sync_table,
                                             g_mk2_palette_sync_status, sizeof g_mk2_palette_sync_status);
            g_mk2_palette_sync_last_rc = ok ? 0 : 1;
            stage_set_toast(ok ? "Runtime palettes synced" : "Runtime palette sync failed");
            if (ok) {
                g_mk2_palette_sync_popup = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!can_sync) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(90, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip This Change", ImVec2(140, 0))) {
            g_mk2_palette_sync_dirty = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
        g_mk2_palette_sync_popup = false;
}

/* Path of the per-stage draft ASM (<dir>\<StageName>.BGND.ASM, next to the
 * BDB/BDD) regardless of whether it exists yet -- used by both the "does a
 * draft exist" check and the "where should I write a new one" creator. */
static void stage_draft_bgnd_compute_path(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    const char *project_path = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (!project_path || !project_path[0] || !g_name[0]) return;
    char dir[512];
    stage_dirname(project_path, dir, sizeof dir);
    char filename[80];
    snprintf(filename, sizeof filename, "%s.BGND.ASM", g_name);
    path_join(out, outsz, dir, filename);
}

/* True only if the per-stage draft actually exists on disk yet. */
bool stage_draft_bgnd_path(char *out, size_t outsz)
{
    char path[640];
    stage_draft_bgnd_compute_path(path, sizeof path);
    if (!path[0] || !stage_file_exists(path)) {
        if (out && outsz) out[0] = '\0';
        return false;
    }
    if (out && outsz) snprintf(out, outsz, "%s", path);
    return true;
}

/* The shared BGND.ASM that ships with the game -- never the per-stage draft.
 * Used by promotion (which always targets the real file) regardless of
 * whether a draft is currently being edited. */
static bool stage_real_bgnd_path(char *out, size_t outsz)
{
    if (!out || outsz == 0) return false;
    out[0] = '\0';
    if (g_stage_start_bgnd_path[0] && stage_file_exists(g_stage_start_bgnd_path)) {
        snprintf(out, outsz, "%s", g_stage_start_bgnd_path);
        return true;
    }
    if (g_runtime_bgnd[0] && stage_file_exists(g_runtime_bgnd)) {
        snprintf(out, outsz, "%s", g_runtime_bgnd);
        return true;
    }
    char path[640];
    if (g_stage_mk2_root[0]) {
        path_join(path, sizeof path, g_stage_mk2_root, "src\\BGND.ASM");
        if (stage_file_exists(path)) {
            snprintf(out, outsz, "%s", path);
            snprintf(g_stage_start_bgnd_path, sizeof g_stage_start_bgnd_path, "%s", path);
            return true;
        }
    }
    const char *project_path = g_bdb_path[0] ? g_bdb_path : g_bdd_path;
    if (project_path && project_path[0]) {
        char dir[512];
        stage_dirname(project_path, dir, sizeof dir);
        path_join(path, sizeof path, dir, "..\\src\\BGND.ASM");
        if (stage_file_exists(path)) {
            snprintf(out, outsz, "%s", path);
            snprintf(g_stage_start_bgnd_path, sizeof g_stage_start_bgnd_path, "%s", path);
            return true;
        }
    }
    return false;
}

/* Every Runtime Binding read/write goes through this: prefer the per-stage
 * draft so nothing touches the shared BGND.ASM until the user explicitly
 * promotes it. */
static bool stage_start_find_bgnd_path(char *out, size_t outsz)
{
    if (stage_draft_bgnd_path(out, outsz))
        return true;
    return stage_real_bgnd_path(out, outsz);
}

/* Best-effort starting point only -- ground_y/scroll limits are heuristics
 * the user is expected to verify, never an authoritative read of intent. */
static int stage_draft_guess_ground_y(void)
{
    int sum = 0, n = 0;
    for (int i = 0; i < g_no; i++) {
        int layer = (g_obj[i].wx >> 8) & 0xFF;
        if (layer != 0x40 && layer != 0x41) continue;
        Img *im = img_find(g_obj[i].ii);
        int h = im ? im->h : 0;
        sum += g_obj[i].sy + h;
        n++;
    }
    int guess = (n > 0) ? (sum / n) : 200;
    if (guess < 0) guess = 0;
    if (guess > 253) guess = 253;
    return guess;
}

bool stage_create_draft_bgnd(void)
{
    if (!g_have_bdb || !g_name[0] || g_bdb_num_modules <= 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Need a saved stage with at least one module before drafting BGND.ASM.");
        return false;
    }
    if (g_bdb_num_modules > 8) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%d modules won't fit -- MK2 background planes top out at 8 (baklst1-8).",
                 g_bdb_num_modules);
        return false;
    }
    char path[640];
    stage_draft_bgnd_compute_path(path, sizeof path);
    if (!path[0]) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Save the stage to a file before drafting BGND.ASM.");
        return false;
    }

    int ww = 1024, wh = 256;
    get_world_size(&ww, &wh);
    int scroll_right = ww - 400;
    if (scroll_right < 0) scroll_right = 0;
    int ground_y = stage_draft_guess_ground_y();

    std::vector<std::string> lines;
    char ln[256];
    lines.push_back("; Draft background module for " + std::string(g_name) +
                    " -- generated by bddtool, not yet part of the shared BGND.ASM.");
    lines.push_back("; Values marked \"verify\" are heuristic starting points, not authored intent.");
    lines.push_back("");
    snprintf(ln, sizeof ln, "%s_mod", g_name); lines.push_back(ln);
    lines.push_back("\t.word\t0\t\t\t; autoerase color (verify)");
    snprintf(ln, sizeof ln, "\t.word\t>%X\t\t\t; ground y (verify)", ground_y); lines.push_back(ln);
    snprintf(ln, sizeof ln, "\t.word\t>%X\t\t\t; initial world y", ground_y); lines.push_back(ln);
    lines.push_back("\t.word\t0\t\t\t; initial worldx");
    lines.push_back("\t.word\t0\t\t\t; scroll left limit");
    snprintf(ln, sizeof ln, "\t.word\t>%X\t\t\t; scroll right limit (verify)", scroll_right); lines.push_back(ln);
    lines.push_back("");
    snprintf(ln, sizeof ln, "\t.long\t%s_calla", g_name); lines.push_back(ln);
    snprintf(ln, sizeof ln, "\t.long\t%s_scroll\t\t; scroll table", g_name); lines.push_back(ln);
    snprintf(ln, sizeof ln, "\t.long\tdlists_%s", g_name); lines.push_back(ln);
    lines.push_back("");
    lines.push_back("\t.long\tbak1mods");
    for (int m = 0; m < g_bdb_num_modules; m++) {
        char mn[64] = "";
        if (sscanf(g_bdb_modules[m], "%63s", mn) != 1) continue;
        snprintf(ln, sizeof ln, "\t.long\t%sBMOD\t\t; baklst%d", mn, m + 1); lines.push_back(ln);
        lines.push_back("\t.word\t0,0");
    }
    lines.push_back("\t.long\t0");
    lines.push_back("");
    snprintf(ln, sizeof ln, "%s_scroll", g_name); lines.push_back(ln);
    for (int slot = 8; slot >= 0; slot--) {
        bool used = slot >= 1 && slot <= g_bdb_num_modules;
        snprintf(ln, sizeof ln, "\t.long\t%s\t\t; %d", used ? ">20000" : "0", slot);
        lines.push_back(ln);
    }
    lines.push_back("");
    snprintf(ln, sizeof ln, "dlists_%s", g_name); lines.push_back(ln);
    lines.push_back("\t.long\tobjlst,worldtlx+16");
    lines.push_back("\t.long\t0");
    lines.push_back("");
    snprintf(ln, sizeof ln, "%s_calla", g_name); lines.push_back(ln);
    lines.push_back("\trets");
    lines.push_back("");

    if (!mk2_write_text_lines(path, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not write %s.", path);
        return false;
    }
    bdd_invalidate_stage_module_cache();
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Created draft %s. Every baklst plane defaults to playfield speed (1.0x) -- "
             "use the parallax slider below to differentiate them, and verify the \"verify\" "
             "fields against your actual art before promoting.", path);
    stage_set_toast("Created draft BGND.ASM");
    return true;
}

static bool stage_start_asm_label_line(const std::string &line, std::string *out)
{
    if (line.empty()) return false;
    unsigned char first = (unsigned char)line[0];
    if (first == ' ' || first == '\t' || first == '*' || first == ';' || first == '.')
        return false;
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
        return false;
    size_t i = 1;
    while (i < line.size()) {
        unsigned char ch = (unsigned char)line[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
            break;
        i++;
    }
    if (out) *out = line.substr(0, i);
    return i > 0;
}

static std::string stage_start_strip_comment(std::string line)
{
    size_t semi = line.find(';');
    if (semi != std::string::npos)
        line.resize(semi);
    return line;
}

static bool stage_start_line_has_symbol(const std::string &line_raw, const std::string &symbol)
{
    std::string line = stage_start_strip_comment(line_raw);
    size_t p = line.find(".long");
    if (p == std::string::npos) return false;
    std::string rest = line.substr(p + 5);
    size_t start = 0;
    while (start <= rest.size()) {
        size_t comma = rest.find(',', start);
        std::string one = mk2_trim_copy(rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (mk2_string_eq_ci(one, symbol))
            return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

static bool stage_start_infer_bgnd_block(const std::vector<std::string> &lines,
                                         char *label_out, size_t label_outsz,
                                         int *line_out)
{
    std::vector<std::string> wanted;
    for (int i = 0; i < g_bdb_num_modules; i++) {
        char mod[64] = "";
        if (sscanf(g_bdb_modules[i], "%63s", mod) == 1 && mod[0]) {
            std::string symbol = mod;
            symbol += "BMOD";
            bool seen = false;
            for (const std::string &old : wanted) {
                if (mk2_string_eq_ci(old, symbol)) {
                    seen = true;
                    break;
                }
            }
            if (!seen) wanted.push_back(symbol);
        }
    }
    if (wanted.empty()) return false;

    for (size_t i = 0; i < lines.size(); i++) {
        bool hit = false;
        for (const std::string &symbol : wanted) {
            if (stage_start_line_has_symbol(lines[i], symbol)) {
                hit = true;
                break;
            }
        }
        if (!hit) continue;

        for (int j = (int)i; j >= 0; j--) {
            std::string label;
            if (!stage_start_asm_label_line(lines[(size_t)j], &label))
                continue;
            if (label.find("dlists_") == 0 || label.find("_scroll") != std::string::npos)
                continue;
            if (label_out && label_outsz) snprintf(label_out, label_outsz, "%s", label.c_str());
            if (line_out) *line_out = j;
            return true;
        }
    }
    return false;
}

static std::string stage_start_asm_int(int value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", value);
    return buf;
}

static bool stage_start_replace_word_line(std::vector<std::string> &lines, int label_line,
                                          int word_index, int value)
{
    int count = 0;
    int end = (int)lines.size();
    for (int i = label_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) {
            end = i;
            break;
        }
    }
    for (int i = label_line + 1; i < end; i++) {
        size_t p = lines[(size_t)i].find(".word");
        if (p == std::string::npos) continue;
        count++;
        if (count != word_index) continue;
        std::string comment;
        size_t semi = lines[(size_t)i].find(';');
        if (semi != std::string::npos)
            comment = lines[(size_t)i].substr(semi);
        std::string indent = lines[(size_t)i].substr(0, p);
        std::string next = indent + ".word\t" + stage_start_asm_int(value);
        if (!comment.empty()) {
            next += "\t";
            next += comment;
        }
        bool changed = (lines[(size_t)i] != next);
        lines[(size_t)i] = next;
        return changed;
    }
    return false;
}

bool stage_start_apply_bgnd_patch(void)
{
    char bgnd[640];
    if (!stage_start_find_bgnd_path(bgnd, sizeof bgnd)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "BGND.ASM was not found.");
        return false;
    }

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnd, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not read BGND.ASM.");
        return false;
    }

    char block_label[96] = "";
    int block_line = -1;
    if (!stage_start_infer_bgnd_block(lines, block_label, sizeof block_label, &block_line)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not infer the BGND init block from current BDB module names.");
        return false;
    }

    bool changed_y = stage_start_replace_word_line(lines, block_line, 3, g_stage_start_camera_y);
    bool changed_x = stage_start_replace_word_line(lines, block_line, 4, g_stage_start_camera_x);
    if (!changed_x && !changed_y) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already uses start camera X=%d Y=%d.",
                 block_label, g_stage_start_camera_x, g_stage_start_camera_y);
        return true;
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnd, ".pre_start_camera_sync", backup, sizeof backup)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not back up BGND.ASM.");
        return false;
    }
    if (!mk2_write_text_lines(bgnd, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not write BGND.ASM; backup: %s", backup);
        return false;
    }

    int removed = 0;
    mk2_palette_sync_remove_stale_outputs(bgnd, &removed);
    bdd_invalidate_stage_module_cache();
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Patched %s start camera to X=%d Y=%d. Backup: %s. Removed %d stale product(s).",
             block_label, g_stage_start_camera_x, g_stage_start_camera_y, backup, removed);
    stage_set_toast("Patched BGND start camera");
    return true;
}

bool stage_start_apply_bgnd_limits(int scroll_left, int scroll_right)
{
    char bgnd[640];
    if (!stage_start_find_bgnd_path(bgnd, sizeof bgnd)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "BGND.ASM was not found.");
        return false;
    }

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnd, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not read BGND.ASM.");
        return false;
    }

    char block_label[96] = "";
    int block_line = -1;
    if (!stage_start_infer_bgnd_block(lines, block_label, sizeof block_label, &block_line)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not infer the BGND init block from current BDB module names.");
        return false;
    }

    /* <stage>_mod words: 3=worldY 4=worldX 5=scroll-left 6=scroll-right. */
    bool changed_l = stage_start_replace_word_line(lines, block_line, 5, scroll_left);
    bool changed_r = stage_start_replace_word_line(lines, block_line, 6, scroll_right);
    if (!changed_l && !changed_r) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already uses scroll limits L=%d R=%d.",
                 block_label, scroll_left, scroll_right);
        return true;
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnd, ".pre_scroll_limit_sync", backup, sizeof backup)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not back up BGND.ASM.");
        return false;
    }
    if (!mk2_write_text_lines(bgnd, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not write BGND.ASM; backup: %s", backup);
        return false;
    }

    int removed = 0;
    mk2_palette_sync_remove_stale_outputs(bgnd, &removed);
    bdd_invalidate_stage_module_cache();
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Patched %s scroll limits to L=%d R=%d. Backup: %s. Removed %d stale product(s).",
             block_label, scroll_left, scroll_right, backup, removed);
    stage_set_toast("Patched BGND scroll limits");
    return true;
}

/* First operand token of a directive line (".long"/".word"), up to whitespace,
   comma or comment. Empty if the directive is not present on the line. */
static std::string bgnd_directive_token(const std::string &raw, const char *dir)
{
    std::string line = raw;
    size_t semi = line.find(';');
    if (semi != std::string::npos) line.resize(semi);
    size_t p = line.find(dir);
    if (p == std::string::npos) return std::string();
    size_t s = p + strlen(dir);
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) s++;
    size_t e = s;
    while (e < line.size() && line[e] != ' ' && line[e] != '\t' && line[e] != ',') e++;
    return line.substr(s, e - s);
}

static bool bgnd_token_ieq(const std::string &a, const char *b)
{
    return mk2_sync_strcasecmp(a.c_str(), b) == 0;
}

/* Within the <stage>_mod block (starting at block_line), find the target
   module's baklst plane index, the line of its ".long <name>BMOD", the following
   ".word x,y" offset line (-1 if none), the block end, and the scroll table
   label (2nd header long). Mirrors bdd_parse_stage_mod_block. */
struct BgndModLoc {
    int baklst;
    int bmod_line;
    int offset_line;
    int block_end;
    std::string scroll_label;
};
static bool bgnd_locate_module(const std::vector<std::string> &lines, int block_line,
                               const char *module_name, BgndModLoc *loc)
{
    loc->baklst = -1;
    loc->bmod_line = -1;
    loc->offset_line = -1;
    loc->block_end = (int)lines.size();
    loc->scroll_label.clear();
    if (block_line < 0) return false;

    int long_count = 0, baklst_num = 0;
    bool want_offset = false;
    int i = block_line + 1;
    for (; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) break;   /* block end */
        std::string longtok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (!longtok.empty()) {
            long_count++;
            if (long_count == 2) loc->scroll_label = longtok;
            if (long_count <= 4) { want_offset = false; continue; }       /* header longs */
            char c0 = longtok[0];
            if (c0 == '>' || (c0 >= '0' && c0 <= '9')) break;             /* numeric ends list */
            baklst_num++;
            want_offset = false;
            if (bgnd_token_ieq(longtok, "skip_bakmod")) continue;
            size_t tl = longtok.size();
            if (tl < 4 || mk2_sync_strcasecmp(longtok.c_str() + tl - 4, "BMOD") != 0) break;
            std::string nm = longtok.substr(0, tl - 4);
            if (bgnd_token_ieq(nm, module_name)) {
                loc->baklst = baklst_num;
                loc->bmod_line = i;
                want_offset = true;                                       /* next .word is its offset */
            }
            continue;
        }
        if (want_offset && !bgnd_directive_token(lines[(size_t)i], ".word").empty()) {
            loc->offset_line = i;
            want_offset = false;
        }
    }
    loc->block_end = i;
    return loc->bmod_line >= 0;
}

static bool bgnd_load_block(std::vector<std::string> &lines, char *block_label, size_t lbsz,
                            int *block_line, char *bgnd_out, size_t bgnd_sz)
{
    if (!stage_start_find_bgnd_path(bgnd_out, bgnd_sz)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "BGND.ASM was not found.");
        return false;
    }
    if (!mk2_read_text_lines(bgnd_out, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not read BGND.ASM.");
        return false;
    }
    if (!stage_start_infer_bgnd_block(lines, block_label, lbsz, block_line)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not infer the BGND init block from current BDB module names.");
        return false;
    }
    return true;
}

static bool bgnd_commit(const char *bgnd, std::vector<std::string> &lines,
                        const char *suffix, char *backup_out, size_t backup_sz)
{
    if (!mk2_copy_file_unique(bgnd, suffix, backup_out, backup_sz)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status, "Could not back up BGND.ASM.");
        return false;
    }
    if (!mk2_write_text_lines(bgnd, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not write BGND.ASM; backup: %s", backup_out);
        return false;
    }
    mk2_palette_sync_remove_stale_outputs(bgnd, NULL);
    bdd_invalidate_stage_module_cache();
    return true;
}

/* Adds a brand-new module to the end of the <stage>_mod BMOD list, claiming the
 * next free background plane. Only ever appends before the list's numeric/">"
 * terminator, so every already-placed module keeps its existing baklst number
 * and scroll-table row untouched -- this is the lowest-risk place to insert.
 * Caller should follow up with stage_bgnd_set_module_parallax to dial in the
 * new plane's scroll speed; it lands at whatever the scroll table's row for
 * the newly-claimed baklst already holds (usually 0/unset). */
bool stage_bgnd_create_module_placement(const char *module_name, int ox, int oy)
{
    if (!module_name || !module_name[0]) return false;
    {
        /* module_name becomes <name>BMOD, a real assembly label/symbol in BGND.ASM --
         * it has to be a legal TI-ASM identifier: start with a letter or underscore,
         * then only letters/digits/underscores. Catching this here (rather than only
         * in the module-name text field) protects every caller of this function. */
        char c0 = module_name[0];
        bool ok = (c0 == '_') || ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'));
        for (const char *p = module_name; ok && *p; p++) {
            char c = *p;
            ok = (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9');
        }
        if (!ok) {
            snprintf(g_stage_start_status, sizeof g_stage_start_status,
                     "\"%s\" isn't a valid module name for BGND.ASM -- it becomes the symbol "
                     "%sBMOD, so it must start with a letter and contain only letters, "
                     "digits, or underscores.", module_name, module_name);
            return false;
        }
    }
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    BgndModLoc existing;
    if (bgnd_locate_module(lines, block_line, module_name, &existing)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s is already placed in %s -- use Apply placement instead.",
                 module_name, block_label);
        return false;
    }

    int long_count = 0, baklst_num = 0, insert_at = -1;
    int i = block_line + 1;
    for (; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) break;
        std::string longtok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (longtok.empty()) continue;
        long_count++;
        if (long_count <= 4) continue;
        char c0 = longtok[0];
        if (c0 == '>' || (c0 >= '0' && c0 <= '9')) { insert_at = i; break; }
        baklst_num++;
    }
    if (insert_at < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find the end of the BMOD list in %s.", block_label);
        return false;
    }
    if (baklst_num >= 8) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already uses all 8 background planes; cannot add %s.",
                 block_label, module_name);
        return false;
    }

    char bmod_line[96], word_line[96];
    snprintf(bmod_line, sizeof bmod_line, "\t.long\t%sBMOD", module_name);
    snprintf(word_line, sizeof word_line, "\t.word\t%d,%d", ox, oy);
    lines.insert(lines.begin() + insert_at, word_line);
    lines.insert(lines.begin() + insert_at, bmod_line);

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_bmod_create", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Placed %s on new plane %d (offset %d,%d) in %s. Backup: %s. "
             "Set its parallax below -- it starts at whatever that plane's scroll row already holds.",
             module_name, baklst_num + 1, ox, oy, block_label, backup);
    stage_set_toast("Created runtime placement");
    return true;
}

bool stage_bgnd_set_module_offset(const char *module_name, int ox, int oy)
{
    if (!module_name || !module_name[0]) return false;
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    BgndModLoc loc;
    if (!bgnd_locate_module(lines, block_line, module_name, &loc)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Module %s is not placed in %s (no %sBMOD entry).",
                 module_name, block_label, module_name);
        return false;
    }

    /* Preserve any trailing comment (e.g. "; castle") when rewriting. */
    std::string comment;
    if (loc.offset_line >= 0) {
        size_t semi = lines[(size_t)loc.offset_line].find(';');
        if (semi != std::string::npos) comment = lines[(size_t)loc.offset_line].substr(semi);
    }
    char wline[192];
    snprintf(wline, sizeof wline, "\t.word\t%d,%d%s%s", ox, oy,
             comment.empty() ? "" : "\t\t", comment.c_str());
    bool changed;
    if (loc.offset_line >= 0) {
        changed = (lines[(size_t)loc.offset_line] != wline);
        lines[(size_t)loc.offset_line] = wline;
    } else {
        lines.insert(lines.begin() + loc.bmod_line + 1, wline);
        changed = true;
    }
    if (!changed) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already placed at runtime offset %d,%d.", module_name, ox, oy);
        return true;
    }

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_module_offset", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Set %s runtime offset to %d,%d. Backup: %s.", module_name, ox, oy, backup);
    stage_set_toast("Patched module runtime offset");
    return true;
}

bool stage_bgnd_set_module_parallax(const char *module_name, float factor)
{
    if (!module_name || !module_name[0]) return false;
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    BgndModLoc loc;
    if (!bgnd_locate_module(lines, block_line, module_name, &loc)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Module %s is not placed in %s.", module_name, block_label);
        return false;
    }
    if (loc.baklst < 1 || loc.baklst > 8 || loc.scroll_label.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s is not on a parallax plane with a scroll table.", module_name);
        return false;
    }
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 2.0f) factor = 2.0f;
    long rate = (long)(factor * 131072.0f + 0.5f);   /* playfield = 0x20000 */

    int sl = -1;
    std::string lbl;
    for (int i = 0; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], &lbl) &&
            bgnd_token_ieq(lbl, loc.scroll_label.c_str())) { sl = i; break; }
    }
    if (sl < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Scroll table %s not found.", loc.scroll_label.c_str());
        return false;
    }

    int target_row = 8 - loc.baklst;            /* table rows are baklst 8..0 */
    int row = 0, found = -1;
    for (int i = sl + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) break;
        if (bgnd_directive_token(lines[(size_t)i], ".long").empty()) continue;
        if (row == target_row) { found = i; break; }
        row++;
    }
    if (found < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Scroll table %s has no row for baklst %d.", loc.scroll_label.c_str(), loc.baklst);
        return false;
    }

    std::string comment;
    size_t semi = lines[(size_t)found].find(';');
    if (semi != std::string::npos) comment = lines[(size_t)found].substr(semi);
    char val[32];
    if (rate == 0) snprintf(val, sizeof val, "0");
    else snprintf(val, sizeof val, "0%lxh", rate);
    char nl[192];
    snprintf(nl, sizeof nl, "\t.long\t%s%s%s",
             val, comment.empty() ? "" : "\t\t", comment.c_str());
    if (lines[(size_t)found] == nl) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s plane (baklst %d) already at parallax %.2fx.", module_name, loc.baklst, factor);
        return true;
    }
    lines[(size_t)found] = nl;

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_plane_parallax", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Set %s plane (baklst %d) parallax to %.2fx (affects all modules on that plane). Backup: %s.",
             module_name, loc.baklst, factor, backup);
    stage_set_toast("Patched plane parallax");
    return true;
}

bool stage_bgnd_set_bg_color(int r5, int g5, int b5)
{
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    /* Word 1 of <stage>_mod (the autoerase/irqskye colour) is the first .word
       in the block, before any .long. */
    int wline = -1;
    for (int i = block_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) break;
        if (!bgnd_directive_token(lines[(size_t)i], ".long").empty()) break;
        if (!bgnd_directive_token(lines[(size_t)i], ".word").empty()) { wline = i; break; }
    }
    if (wline < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find the background colour word in %s.", block_label);
        return false;
    }

    if (r5 < 0) r5 = 0; if (r5 > 31) r5 = 31;
    if (g5 < 0) g5 = 0; if (g5 > 31) g5 = 31;
    if (b5 < 0) b5 = 0; if (b5 > 31) b5 = 31;

    std::string comment;
    size_t semi = lines[(size_t)wline].find(';');
    if (semi != std::string::npos) comment = lines[(size_t)wline].substr(semi);
    char nl[192];
    snprintf(nl, sizeof nl, "\t.word\t(32*32*%d)+(32*%d)+%d%s%s",
             r5, g5, b5, comment.empty() ? "" : "\t\t", comment.c_str());
    if (lines[(size_t)wline] == nl) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s background colour already set.", block_label);
        return true;
    }
    lines[(size_t)wline] = nl;

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_bg_color", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Set %s background colour to RGB555 %d,%d,%d. Backup: %s.",
             block_label, r5, g5, b5, backup);
    stage_set_toast("Patched stage background colour");
    return true;
}

/* Merge the per-stage draft into the real, shared BGND.ASM: a datestamped
 * backup of the current file is made first, then a new file is written with
 * the draft's block appended and a table_o_mods entry added at the next
 * free slot. MKSEL.ASM stage-select wiring is intentionally out of scope --
 * that's still a manual step, same as it was before drafts existed. */
bool stage_promote_draft_to_bgnd(void)
{
    char draft_path[640];
    if (!stage_draft_bgnd_path(draft_path, sizeof draft_path)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "No draft BGND.ASM for this stage to promote.");
        return false;
    }
    std::vector<std::string> draft_lines;
    if (!mk2_read_text_lines(draft_path, draft_lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not read %s.", draft_path);
        return false;
    }

    char real_path[640];
    if (!stage_real_bgnd_path(real_path, sizeof real_path)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not locate the real BGND.ASM to promote into.");
        return false;
    }

    char ts[32];
    time_t now = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    strftime(ts, sizeof ts, ".bak_%Y%m%d_%H%M%S", &tmv);

    char backup[640] = "";
    if (!mk2_copy_file_unique(real_path, ts, backup, sizeof backup)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not back up %s before promoting.", real_path);
        return false;
    }

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(real_path, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not read %s.", real_path);
        return false;
    }

    /* Find table_o_mods and the line after its last .long entry. */
    int table_label_line = -1;
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string label;
        if (stage_start_asm_label_line(lines[(size_t)i], &label) &&
            mk2_sync_strcasecmp(label.c_str(), "table_o_mods") == 0) {
            table_label_line = i;
            break;
        }
    }
    if (table_label_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find table_o_mods in %s -- not promoting.", real_path);
        return false;
    }
    int last_long_line = -1, next_index = 0;
    for (int i = table_label_line + 1; i < (int)lines.size(); i++) {
        if (lines[(size_t)i].empty()) continue;
        if (!bgnd_directive_token(lines[(size_t)i], ".long").empty()) {
            last_long_line = i;
            next_index++;
            continue;
        }
        break;
    }
    if (last_long_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "table_o_mods in %s has no entries to insert after -- not promoting.", real_path);
        return false;
    }

    /* Find the file's closing .end directive; the draft block lands right
       before it. Search from the end since .end must be the last real line. */
    int end_line = -1;
    for (int i = (int)lines.size() - 1; i >= 0; i--) {
        std::string trimmed = lines[(size_t)i];
        size_t a = trimmed.find_first_not_of(" \t");
        size_t b = trimmed.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        trimmed = trimmed.substr(a, b - a + 1);
        if (mk2_sync_strcasecmp(trimmed.c_str(), ".end") == 0) { end_line = i; break; }
        if (!trimmed.empty()) break;  /* hit real content before finding .end */
    }
    if (end_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find the closing .end in %s -- not promoting.", real_path);
        return false;
    }

    char entry[128];
    snprintf(entry, sizeof entry, "\t.long\t%s_mod\t; %d - %s", g_name, next_index, g_name);
    lines.insert(lines.begin() + last_long_line + 1, entry);
    end_line++;  /* shift past the insertion above */

    std::vector<std::string> merged;
    merged.insert(merged.end(), draft_lines.begin(), draft_lines.end());
    merged.push_back("");
    lines.insert(lines.begin() + end_line, merged.begin(), merged.end());

    if (!mk2_write_text_lines(real_path, lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not write %s; your draft and backup (%s) are untouched.",
                 real_path, backup);
        return false;
    }

    char promoted_path[680];
    snprintf(promoted_path, sizeof promoted_path, "%s.promoted", draft_path);
    remove(promoted_path);
    rename(draft_path, promoted_path);
    bdd_invalidate_stage_module_cache();

    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Promoted %s into %s as table_o_mods entry %d. Backup: %s.",
             g_name, real_path, next_index, backup);
    stage_set_toast("Promoted stage to BGND.ASM");
    return true;
}

