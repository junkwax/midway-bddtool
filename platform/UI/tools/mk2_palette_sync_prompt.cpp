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
    const char *base_suffix = (suffix && suffix[0]) ? suffix : ".bak";
    char dst[640];
    bool found = false;
    for (int i = 0; i < 100 && !found; i++) {
        char unique_suffix[128];
        if (i == 0) snprintf(unique_suffix, sizeof unique_suffix, "%s", base_suffix);
        else snprintf(unique_suffix, sizeof unique_suffix, "%s.%d", base_suffix, i);
        if (!bddtool_backup_path(dst, sizeof dst, src, unique_suffix, "asm")) {
            fclose(in);
            return false;
        }
        if (!stage_file_exists(dst))
            found = true;
    }
    if (!found) {
        char stamp[32] = ".next";
        time_t now = time(NULL);
        struct tm tmv = {};
#ifdef _WIN32
        if (localtime_s(&tmv, &now) == 0)
#else
        if (localtime_r(&now, &tmv))
#endif
            strftime(stamp, sizeof stamp, ".%Y%m%d_%H%M%S", &tmv);

        for (int i = 0; i < 10000 && !found; i++) {
            char unique_suffix[128];
            if (i == 0) snprintf(unique_suffix, sizeof unique_suffix, "%s%s", base_suffix, stamp);
            else snprintf(unique_suffix, sizeof unique_suffix, "%s%s.%d", base_suffix, stamp, i);
            if (!bddtool_backup_path(dst, sizeof dst, src, unique_suffix, "asm")) {
                fclose(in);
                return false;
            }
            if (!stage_file_exists(dst))
                found = true;
        }
    }
    if (!found) { fclose(in); return false; }
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
   ~18.8 KB; its .DATA links into the program-ROM palette window next to the
   already-packed MK8MIL movie payload. FLAPJACK art is promoted through its
   video-ROM sidecar, but shared palette tables still need this cap. */
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
   The whole file is .DATA, so this equals the BGNDPAL.OBJ footprint in the
   program-ROM palette window next to the already-packed MK8MIL payload. */
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

static void mk2_sync_project_basename_key(char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!g_bdb_path[0] && !g_bdd_path[0]) return;
    mk2_sync_stage_basename_no_ext(g_bdb_path[0] ? g_bdb_path : g_bdd_path, out, outsz);
    mk2_sync_uppercase_ascii_inplace(out);
}

static void mk2_push_unique_ci(std::vector<std::string> &list, const std::string &value)
{
    if (value.empty()) return;
    for (const std::string &old : list)
        if (mk2_string_eq_ci(old, value))
            return;
    list.push_back(value);
}

static bool mk2_palette_sync_infer_table_from_bgndtbl(const char *bgnpal, char *out, size_t outsz)
{
    out[0] = '\0';
    char tbl[640];
    if (!mk2_palette_sync_bgndtbl_for_bgnpal(bgnpal, tbl, sizeof tbl))
        return false;

    std::vector<std::string> wanted_blks;
    std::vector<std::string> wanted_bmods;
    for (int i = 0; i < g_bdb_num_modules; i++) {
        char mod[64] = "";
        if (sscanf(g_bdb_modules[i], "%63s", mod) == 1 && mod[0]) {
            std::string w = mod;
            w += "BLKS";
            mk2_push_unique_ci(wanted_blks, w);
            w = mod;
            w += "BMOD";
            mk2_push_unique_ci(wanted_bmods, w);
        }
    }
    char stage_key[96] = "";
    mk2_palette_sync_stage_key(stage_key, sizeof stage_key);
    if (stage_key[0]) {
        mk2_push_unique_ci(wanted_blks, std::string(stage_key) + "BLKS");
        mk2_push_unique_ci(wanted_bmods, std::string(stage_key) + "BMOD");
    }
    char file_key[96] = "";
    mk2_sync_project_basename_key(file_key, sizeof file_key);
    if (file_key[0]) {
        mk2_push_unique_ci(wanted_blks, std::string(file_key) + "BLKS");
        mk2_push_unique_ci(wanted_bmods, std::string(file_key) + "BMOD");
    }
    if (wanted_blks.empty() && wanted_bmods.empty()) return false;

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(tbl, lines)) return false;
    std::string current_label;
    for (const std::string &line_raw : lines) {
        std::string label;
        if (mk2_label_name_from_line(line_raw, &label))
            current_label = label;

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
        for (const std::string &w : wanted_blks) {
            if (mk2_string_eq_ci(tok[0], w)) {
                snprintf(out, outsz, "%s", tok[2].c_str());
                return true;
            }
        }
        for (const std::string &w : wanted_bmods) {
            if (mk2_string_eq_ci(current_label, w)) {
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

static bool mk2_palette_sync_build_candidate(const char *bgnpal, const char *table_label,
                                             std::vector<std::string> &lines,
                                             bool *changed_out,
                                             char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    lines.clear();
    if (changed_out) *changed_out = false;
    if (!bgnpal || !bgnpal[0] || !stage_file_exists(bgnpal)) {
        if (status && statussz) snprintf(status, statussz, "BGNDPAL.ASM path is missing.");
        return false;
    }
    if (!table_label || !table_label[0]) {
        if (status && statussz) snprintf(status, statussz, "Palette table label is missing.");
        return false;
    }
    if (g_n_pals <= 0) {
        if (status && statussz) snprintf(status, statussz, "Current BDD has no palettes to sync.");
        return false;
    }
    if (!mk2_read_text_lines(bgnpal, lines)) {
        if (status && statussz) snprintf(status, statussz, "Could not read %s", bgnpal);
        return false;
    }
    int table_start = mk2_find_label_line(lines, table_label);
    if (table_start < 0) {
        if (status && statussz) snprintf(status, statussz, "Palette table %s was not found.", table_label);
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

    if (changed_out) *changed_out = changed;
    return true;
}

static bool mk2_palette_sync_projected_bytes(const char *bgnpal, const char *table_label,
                                             long *bytes_out, bool *changed_out)
{
    std::vector<std::string> lines;
    bool changed = false;
    char status[256];
    if (!mk2_palette_sync_build_candidate(bgnpal, table_label, lines, &changed,
                                          status, sizeof status))
        return false;
    if (bytes_out) *bytes_out = mk2_bgndpal_assembled_bytes(lines);
    if (changed_out) *changed_out = changed;
    return true;
}

static bool mk2_palette_sync_apply(const char *bgnpal, const char *table_label,
                                   char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    g_mk2_palette_sync_output[0] = '\0';
    std::vector<std::string> lines;
    bool changed = false;
    if (!mk2_palette_sync_build_candidate(bgnpal, table_label, lines, &changed,
                                          status, statussz))
        return false;

    if (!changed) {
        snprintf(status, statussz, "Runtime palette table is already synced.");
        snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
                 "%s already matches %d current BDD palette(s).", table_label, g_n_pals);
        g_mk2_palette_sync_dirty = false;
        return true;
    }

    long assembled = mk2_bgndpal_assembled_bytes(lines);
    if (assembled > MK2_BGNDPAL_BUDGET_BYTES && !g_mk2_palette_allow_over_budget) {
        long over_by = assembled - MK2_BGNDPAL_BUDGET_BYTES;
        snprintf(status, statussz,
                 "Refused: BGNDPAL.ASM would assemble to %ld bytes (budget %d, over by "
                 "%ld). This risks growing program-ROM palette data into the "
                 "already-packed MK8MIL movie payload. Run "
                 "\"Compact duplicates\" first, reduce palette colours, or enable the "
                 "over-budget override.",
                 assembled, MK2_BGNDPAL_BUDGET_BYTES, over_by);
        snprintf(g_mk2_palette_sync_output, sizeof g_mk2_palette_sync_output,
                 "Not written. Assembled palette data %ld bytes exceeds the %d-byte budget by %ld bytes.",
                 assembled, MK2_BGNDPAL_BUDGET_BYTES, over_by);
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
                    ImGui::SetTooltip("BGNDPAL.ASM is program-ROM palette data near the packed MK8MIL\n"
                                      "movie payload. FLAPJACK art stays in its video-ROM sidecar,\n"
                                      "but duplicate shared palettes can still bloat this table.");
                long projected_bytes = 0;
                bool projected_changed = false;
                bool projected_ok = g_mk2_palette_sync_table[0] && g_n_pals > 0 &&
                    mk2_palette_sync_projected_bytes(g_mk2_palette_sync_asm,
                                                     g_mk2_palette_sync_table,
                                                     &projected_bytes,
                                                     &projected_changed);
                bool projected_over = projected_ok &&
                    projected_bytes > MK2_BGNDPAL_BUDGET_BYTES;
                if (projected_ok && projected_changed && projected_bytes != bytes) {
                    long projected_over_by = projected_bytes - MK2_BGNDPAL_BUDGET_BYTES;
                    long projected_delta = projected_over ? projected_over_by : -projected_over_by;
                    ImGui::TextColored(projected_over ? ImVec4(1.0f, 0.45f, 0.30f, 1.0f)
                                                      : ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                                       "After sync: %.1f / %.1f KB%s%ld bytes",
                                       projected_bytes / 1024.0,
                                       MK2_BGNDPAL_BUDGET_BYTES / 1024.0,
                                       projected_over ? "   OVER by " : "   under by ",
                                       projected_delta);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Projected size after replacing %s with the current BDD palettes.",
                                          g_mk2_palette_sync_table);
                }
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
                if (over || projected_over) {
                    ImGui::SameLine();
                    ImGui::Checkbox("Override budget", &g_mk2_palette_allow_over_budget);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Write past the budget anyway. Use only after a ROM build/map check confirms this stage has room.");
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
    int ground_y = g_stage_start_ground_enabled ? g_stage_start_ground_y
                                                : stage_draft_guess_ground_y();

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
    if (g_name[0]) {
        char want_label[70];
        snprintf(want_label, sizeof want_label, "%s_mod", g_name);
        for (size_t i = 0; i < lines.size(); i++) {
            std::string label;
            if (stage_start_asm_label_line(lines[i], &label) &&
                mk2_sync_strcasecmp(label.c_str(), want_label) == 0) {
                if (label_out && label_outsz) snprintf(label_out, label_outsz, "%s", want_label);
                if (line_out) *line_out = (int)i;
                return true;
            }
        }
    }

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

bool stage_start_apply_bgnd_ground(int ground_y)
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

    bool changed = stage_start_replace_word_line(lines, block_line, 2, ground_y);
    if (!changed) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already uses fighter ground Y=%d.", block_label, ground_y);
        return true;
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnd, ".pre_start_ground_sync", backup, sizeof backup)) {
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
             "Patched %s fighter ground Y to %d. Backup: %s. Removed %d stale product(s).",
             block_label, ground_y, backup, removed);
    stage_set_toast("Patched fighter start Y");
    return true;
}

bool stage_start_apply_bgnd_start_placement(void)
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

    bool changed_ground = stage_start_replace_word_line(lines, block_line, 2, g_stage_start_ground_y);
    bool changed_y = stage_start_replace_word_line(lines, block_line, 3, g_stage_start_camera_y);
    bool changed_x = stage_start_replace_word_line(lines, block_line, 4, g_stage_start_camera_x);
    if (!changed_ground && !changed_x && !changed_y) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already uses camera X=%d Y=%d and fighter ground Y=%d.",
                 block_label, g_stage_start_camera_x, g_stage_start_camera_y,
                 g_stage_start_ground_y);
        return true;
    }

    char backup[640] = "";
    if (!mk2_copy_file_unique(bgnd, ".pre_match_start_sync", backup, sizeof backup)) {
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
             "Patched %s match start: camera X=%d Y=%d, fighter ground Y=%d. Backup: %s. Removed %d stale product(s).",
             block_label, g_stage_start_camera_x, g_stage_start_camera_y,
             g_stage_start_ground_y, backup, removed);
    stage_set_toast("Patched match start placement");
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
    std::string dlists_label;
};
static bool bgnd_locate_module(const std::vector<std::string> &lines, int block_line,
                               const char *module_name, BgndModLoc *loc)
{
    loc->baklst = -1;
    loc->bmod_line = -1;
    loc->offset_line = -1;
    loc->block_end = (int)lines.size();
    loc->scroll_label.clear();
    loc->dlists_label.clear();
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
            if (long_count == 3) loc->dlists_label = longtok;
            if (long_count <= 4) { want_offset = false; continue; }       /* header longs */
            char c0 = longtok[0];
            if (c0 == '>' || c0 == '-' || (c0 >= '0' && c0 <= '9')) break;/* numeric ends list */
            if (bgnd_token_ieq(longtok, "center_x")) break;               /* post-list helper */
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

/* Finds a column-0 label matching `want` exactly (case-insensitive). Used so
 * a draft's deterministic "<StageName>_mod" label can be located directly,
 * without depending on any *current* module name still matching a BMOD
 * reference inside it -- the same problem fixed on the read side in
 * viewer_geometry.cpp's bdd_draft_stage_mod_label_if_present, mirrored here
 * for the write side (Apply placement/parallax/offset, "Set as runtime
 * location") so it doesn't regress after the first module in a stage gets
 * renamed or a brand-new module is added that isn't in the block yet. */
static bool bgnd_find_exact_label_line(const std::vector<std::string> &lines,
                                       const char *want, int *line_out)
{
    if (!want || !want[0]) return false;
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string label;
        if (stage_start_asm_label_line(lines[(size_t)i], &label) &&
            mk2_sync_strcasecmp(label.c_str(), want) == 0) {
            if (line_out) *line_out = i;
            return true;
        }
    }
    return false;
}

static bool bgnd_label_ends_with_ci(const std::string &label, const char *suffix)
{
    if (!suffix) return false;
    size_t n = label.size();
    size_t s = strlen(suffix);
    return n >= s && mk2_sync_strcasecmp(label.c_str() + n - s, suffix) == 0;
}

static bool bgnd_block_scroll_label(const std::vector<std::string> &lines,
                                    int block_line, std::string *scroll_label)
{
    if (scroll_label) scroll_label->clear();
    if (block_line < 0) return false;
    int long_count = 0;
    for (int i = block_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        std::string tok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (tok.empty()) continue;
        long_count++;
        if (long_count == 2) {
            if (scroll_label) *scroll_label = tok;
            return true;
        }
    }
    return false;
}

static bool bgnd_find_first_mod_block_with_scroll(const std::vector<std::string> &lines,
                                                  char *label_out, size_t label_sz,
                                                  int *line_out,
                                                  std::string *scroll_label)
{
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string label;
        if (!stage_start_asm_label_line(lines[(size_t)i], &label))
            continue;
        if (!bgnd_label_ends_with_ci(label, "_mod"))
            continue;
        std::string scroll;
        if (!bgnd_block_scroll_label(lines, i, &scroll))
            continue;
        if (label_out && label_sz) snprintf(label_out, label_sz, "%s", label.c_str());
        if (line_out) *line_out = i;
        if (scroll_label) *scroll_label = scroll;
        return true;
    }
    return false;
}

static bool bgnd_find_existing_stage_block(const std::vector<std::string> &lines,
                                           char *label_out, size_t label_sz,
                                           int *line_out,
                                           std::string *scroll_label)
{
    std::vector<std::string> candidates;
    char file_key[96] = "";
    mk2_sync_project_basename_key(file_key, sizeof file_key);
    if (file_key[0])
        mk2_push_unique_ci(candidates, std::string(file_key) + "_mod");
    char stage_key[96] = "";
    mk2_palette_sync_stage_key(stage_key, sizeof stage_key);
    if (stage_key[0])
        mk2_push_unique_ci(candidates, std::string(stage_key) + "_mod");

    for (const std::string &want : candidates) {
        int line = -1;
        std::string scroll;
        if (bgnd_find_exact_label_line(lines, want.c_str(), &line) &&
            bgnd_block_scroll_label(lines, line, &scroll)) {
            if (label_out && label_sz) snprintf(label_out, label_sz, "%s", want.c_str());
            if (line_out) *line_out = line;
            if (scroll_label) *scroll_label = scroll;
            return true;
        }
    }

    int inferred_line = -1;
    char inferred_label[96] = "";
    if (stage_start_infer_bgnd_block(lines, inferred_label, sizeof inferred_label,
                                     &inferred_line)) {
        std::string scroll;
        if (bgnd_block_scroll_label(lines, inferred_line, &scroll)) {
            if (label_out && label_sz) snprintf(label_out, label_sz, "%s", inferred_label);
            if (line_out) *line_out = inferred_line;
            if (scroll_label) *scroll_label = scroll;
            return true;
        }
    }
    return false;
}

static bool bgnd_read_scroll_rows(const std::vector<std::string> &lines,
                                  const std::string &scroll_label,
                                  std::vector<std::string> &rows,
                                  char *status, size_t statussz)
{
    rows.clear();
    int label_line = -1;
    if (!bgnd_find_exact_label_line(lines, scroll_label.c_str(), &label_line)) {
        if (status && statussz)
            snprintf(status, statussz, "Scroll table %s was not found.", scroll_label.c_str());
        return false;
    }
    for (int i = label_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        std::string tok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (tok.empty()) continue;
        rows.push_back(tok);
        if (rows.size() == 9) break;
    }
    if (rows.size() != 9) {
        if (status && statussz)
            snprintf(status, statussz, "Scroll table %s has %d rows; expected 9.",
                     scroll_label.c_str(), (int)rows.size());
        rows.clear();
        return false;
    }
    return true;
}

static bool bgnd_replace_scroll_rows(std::vector<std::string> &lines,
                                     const std::string &scroll_label,
                                     const std::vector<std::string> &rows,
                                     bool *changed_out,
                                     char *status, size_t statussz)
{
    if (changed_out) *changed_out = false;
    if (rows.size() != 9) {
        if (status && statussz)
            snprintf(status, statussz, "Need 9 draft scroll rows before promotion.");
        return false;
    }
    int label_line = -1;
    if (!bgnd_find_exact_label_line(lines, scroll_label.c_str(), &label_line)) {
        if (status && statussz)
            snprintf(status, statussz, "Target scroll table %s was not found.",
                     scroll_label.c_str());
        return false;
    }
    bool changed = false;
    int row = 0;
    for (int i = label_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        if (bgnd_directive_token(lines[(size_t)i], ".long").empty())
            continue;
        std::string comment;
        size_t semi = lines[(size_t)i].find(';');
        if (semi != std::string::npos)
            comment = lines[(size_t)i].substr(semi);
        char nl[192];
        snprintf(nl, sizeof nl, "\t.long\t%s%s%s",
                 rows[(size_t)row].c_str(),
                 comment.empty() ? "" : "\t\t", comment.c_str());
        if (lines[(size_t)i] != nl) {
            lines[(size_t)i] = nl;
            changed = true;
        }
        row++;
        if (row == 9) break;
    }
    if (row != 9) {
        if (status && statussz)
            snprintf(status, statussz, "Target scroll table %s has %d rows; expected 9.",
                     scroll_label.c_str(), row);
        return false;
    }
    if (changed_out) *changed_out = changed;
    return true;
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
    if (g_name[0]) {
        char want[70];
        snprintf(want, sizeof want, "%s_mod", g_name);
        int found_line = -1;
        if (bgnd_find_exact_label_line(lines, want, &found_line)) {
            if (block_label && lbsz) snprintf(block_label, lbsz, "%s", want);
            if (block_line) *block_line = found_line;
            return true;
        }
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
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not back up %s before writing BGND.ASM edits.",
                 bgnd && bgnd[0] ? bgnd : "BGND.ASM");
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

struct BgndBmodEntry {
    int baklst;
    int bmod_line;
    int offset_line;
    bool skip;
    std::string module;
};

static bool bgnd_token_is_numeric_end(const std::string &tok)
{
    if (tok.empty()) return false;
    char c = tok[0];
    return c == '>' || c == '-' || (c >= '0' && c <= '9');
}

static std::string bgnd_bmod_line_for(const std::string &module, int baklst)
{
    char line[160];
    snprintf(line, sizeof line, "\t.long\t%sBMOD\t\t; baklst%d",
             module.c_str(), baklst);
    return std::string(line);
}

static std::string bgnd_skip_line_for(int baklst)
{
    char line[96];
    snprintf(line, sizeof line, "\t.long\tskip_bakmod\t; baklst%d", baklst);
    return std::string(line);
}

static bool bgnd_collect_bmod_entries(const std::vector<std::string> &lines,
                                      int block_line,
                                      std::vector<BgndBmodEntry> &entries)
{
    entries.clear();
    if (block_line < 0)
        return false;

    int long_count = 0;
    int baklst_num = 0;
    int pending = -1;
    for (int i = block_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;

        std::string longtok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (!longtok.empty()) {
            pending = -1;
            long_count++;
            if (long_count <= 4)
                continue;
            if (bgnd_token_is_numeric_end(longtok) ||
                bgnd_token_ieq(longtok, "center_x"))
                break;

            baklst_num++;
            BgndBmodEntry entry;
            entry.baklst = baklst_num;
            entry.bmod_line = i;
            entry.offset_line = -1;
            entry.skip = bgnd_token_ieq(longtok, "skip_bakmod");
            entry.module.clear();

            if (!entry.skip) {
                size_t tl = longtok.size();
                if (tl < 4 || mk2_sync_strcasecmp(longtok.c_str() + tl - 4, "BMOD") != 0)
                    break;
                entry.module = longtok.substr(0, tl - 4);
            }

            entries.push_back(entry);
            if (!entries.back().skip)
                pending = (int)entries.size() - 1;
            continue;
        }

        if (pending >= 0 && !bgnd_directive_token(lines[(size_t)i], ".word").empty()) {
            entries[(size_t)pending].offset_line = i;
            pending = -1;
        }
    }
    return !entries.empty();
}

static int bgnd_parse_baklst_token(const std::string &tok)
{
    if (tok.size() < 6 || mk2_sync_strcasecmp(tok.substr(0, 6).c_str(), "baklst") != 0)
        return -1;
    if (tok.size() == 6)
        return 1;
    int n = atoi(tok.c_str() + 6);
    return (n >= 1 && n <= 8) ? n : -1;
}

struct BgndDlistEntry {
    int baklst;
    int line;
};

static bool bgnd_collect_dlist_baklst_lines(const std::vector<std::string> &lines,
                                            const std::string &dlists_label,
                                            std::vector<BgndDlistEntry> &entries,
                                            char *status, size_t statussz)
{
    entries.clear();
    int label_line = -1;
    if (dlists_label.empty() ||
        !bgnd_find_exact_label_line(lines, dlists_label.c_str(), &label_line)) {
        if (status && statussz)
            snprintf(status, statussz, "Display list %s was not found.",
                     dlists_label.empty() ? "(blank)" : dlists_label.c_str());
        return false;
    }

    for (int i = label_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        std::string tok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (tok.empty())
            continue;
        int baklst = bgnd_parse_baklst_token(tok);
        if (baklst >= 1) {
            BgndDlistEntry entry;
            entry.baklst = baklst;
            entry.line = i;
            entries.push_back(entry);
            continue;
        }
        if (bgnd_token_ieq(tok, "-1"))
            continue;               /* floor_code slot can sit between planes */
        if (bgnd_token_ieq(tok, "-2") ||
            bgnd_token_ieq(tok, "objlst") ||
            bgnd_token_ieq(tok, "objlst2"))
            continue;               /* actor/shadow slots can sit between planes */
        break;                       /* 0, a numeric terminator, or any non-plane */
    }

    if (entries.empty()) {
        if (status && statussz)
            snprintf(status, statussz, "Display list %s has no baklst rows.",
                     dlists_label.c_str());
        return false;
    }
    return true;
}

bool stage_bgnd_swap_module_baklst(const char *module_name, int target_baklst)
{
    if (!module_name || !module_name[0])
        return false;
    if (target_baklst < 1 || target_baklst > 8) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Target plane must be baklst1 through baklst8.");
        return false;
    }

    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    std::vector<BgndBmodEntry> entries;
    if (!bgnd_collect_bmod_entries(lines, block_line, entries)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not read the baklst module list in %s.", block_label);
        return false;
    }

    int src_idx = -1;
    int dst_idx = -1;
    for (int i = 0; i < (int)entries.size(); i++) {
        if (!entries[(size_t)i].skip &&
            bgnd_token_ieq(entries[(size_t)i].module, module_name))
            src_idx = i;
        if (entries[(size_t)i].baklst == target_baklst)
            dst_idx = i;
    }
    if (src_idx < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Module %s is not placed in %s.", module_name, block_label);
        return false;
    }
    BgndBmodEntry src = entries[(size_t)src_idx];
    if (src.baklst == target_baklst) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s is already on baklst%d.", module_name, target_baklst);
        return true;
    }
    if (dst_idx < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s does not list baklst%d; add or free that plane first.",
                 block_label, target_baklst);
        return false;
    }
    if (src.offset_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no offset line to move with its BMOD entry.", module_name);
        return false;
    }

    BgndBmodEntry dst = entries[(size_t)dst_idx];
    if (dst.skip) {
        std::string src_offset = lines[(size_t)src.offset_line];
        int src_line = src.bmod_line;
        int src_offset_line = src.offset_line;
        int dst_line = dst.bmod_line;

        if (dst_line < src_line) {
            lines[(size_t)dst_line] = bgnd_bmod_line_for(src.module, dst.baklst);
            lines.insert(lines.begin() + dst_line + 1, src_offset);
            src_line++;
            src_offset_line++;
            lines[(size_t)src_line] = bgnd_skip_line_for(src.baklst);
            lines.erase(lines.begin() + src_offset_line);
        } else {
            lines[(size_t)src_line] = bgnd_skip_line_for(src.baklst);
            lines.erase(lines.begin() + src_offset_line);
            if (dst_line > src_offset_line)
                dst_line--;
            lines[(size_t)dst_line] = bgnd_bmod_line_for(src.module, dst.baklst);
            lines.insert(lines.begin() + dst_line + 1, src_offset);
        }
    } else {
        if (dst.offset_line < 0) {
            snprintf(g_stage_start_status, sizeof g_stage_start_status,
                     "%s on baklst%d has no offset line to swap.",
                     dst.module.c_str(), dst.baklst);
            return false;
        }
        std::string src_offset = lines[(size_t)src.offset_line];
        std::string dst_offset = lines[(size_t)dst.offset_line];
        lines[(size_t)src.bmod_line] = bgnd_bmod_line_for(dst.module, src.baklst);
        lines[(size_t)src.offset_line] = dst_offset;
        lines[(size_t)dst.bmod_line] = bgnd_bmod_line_for(src.module, dst.baklst);
        lines[(size_t)dst.offset_line] = src_offset;
    }

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_baklst_swap", backup, sizeof backup))
        return false;

    if (dst.skip) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Moved %s from baklst%d to empty baklst%d. Backup: %s.",
                 module_name, src.baklst, target_baklst, backup);
        stage_set_toast("Moved runtime plane");
    } else {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Swapped %s baklst%d with %s baklst%d. Backup: %s.",
                 module_name, src.baklst, dst.module.c_str(), dst.baklst, backup);
        stage_set_toast("Swapped runtime planes");
    }
    return true;
}

bool stage_bgnd_move_module_draw_order(const char *module_name, int direction)
{
    if (!module_name || !module_name[0])
        return false;
    if (direction == 0)
        return true;

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
    if (loc.baklst < 1 || loc.baklst > 8 || loc.dlists_label.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no display-list baklst row to move.", module_name);
        return false;
    }

    std::vector<BgndDlistEntry> entries;
    if (!bgnd_collect_dlist_baklst_lines(lines, loc.dlists_label, entries,
                                         g_stage_start_status,
                                         sizeof g_stage_start_status))
        return false;

    int cur = -1;
    for (int i = 0; i < (int)entries.size(); i++) {
        if (entries[(size_t)i].baklst == loc.baklst) {
            cur = i;
            break;
        }
    }
    if (cur < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s uses baklst%d, but %s does not draw that plane.",
                 module_name, loc.baklst, loc.dlists_label.c_str());
        return false;
    }

    int dst = cur + (direction < 0 ? -1 : 1);
    if (dst < 0 || dst >= (int)entries.size()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s is already at the %s of %s.",
                 module_name, direction < 0 ? "back" : "front",
                 loc.dlists_label.c_str());
        return true;
    }

    std::swap(lines[(size_t)entries[(size_t)cur].line],
              lines[(size_t)entries[(size_t)dst].line]);

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_dlist_order", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Moved %s draw order %s in %s. Backup: %s.",
             module_name, direction < 0 ? "earlier/back" : "later/front",
             loc.dlists_label.c_str(), backup);
    stage_set_toast(direction < 0 ? "Moved plane backward" : "Moved plane forward");
    return true;
}

bool stage_bgnd_reset_foreground_to_module(const char *module_name)
{
    if (!module_name || !module_name[0])
        return false;

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
    if (loc.baklst < 1 || loc.baklst > 8 || loc.dlists_label.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no display-list baklst row to use as foreground.",
                 module_name);
        return false;
    }

    int label_line = -1;
    if (!bgnd_find_exact_label_line(lines, loc.dlists_label.c_str(), &label_line)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Display list %s was not found.", loc.dlists_label.c_str());
        return false;
    }

    std::vector<std::string> back_planes;
    std::vector<std::string> pre_object_slots;
    std::vector<std::string> post_object_slots;
    std::vector<std::string> passthrough;
    std::string target_row;
    std::string objlst_row;
    std::string terminator_row;
    int body_start = label_line + 1;
    int body_end = -1;
    char want[32];
    snprintf(want, sizeof want, "baklst%d", loc.baklst);

    for (int i = body_start; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        std::string tok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (tok.empty()) {
            passthrough.push_back(lines[(size_t)i]);
            continue;
        }

        if (bgnd_token_ieq(tok, "0") || bgnd_token_ieq(tok, ">0")) {
            terminator_row = lines[(size_t)i];
            body_end = i;
            break;
        }

        int baklst = bgnd_parse_baklst_token(tok);
        if (baklst >= 1) {
            if (bgnd_token_ieq(tok, want))
                target_row = lines[(size_t)i];
            else
                back_planes.push_back(lines[(size_t)i]);
            continue;
        }

        if (bgnd_token_ieq(tok, "objlst")) {
            if (objlst_row.empty())
                objlst_row = lines[(size_t)i];
            else
                post_object_slots.push_back(lines[(size_t)i]);
            continue;
        }

        if (bgnd_token_ieq(tok, "objlst2")) {
            post_object_slots.push_back(lines[(size_t)i]);
            continue;
        }

        if (bgnd_token_ieq(tok, "-1") || bgnd_token_ieq(tok, "-2")) {
            pre_object_slots.push_back(lines[(size_t)i]);
            continue;
        }

        /* Unknown display entries are safer left before objlst than dropped or
         * moved in front of fighters. */
        pre_object_slots.push_back(lines[(size_t)i]);
    }

    if (body_end < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no .long 0 terminator.", loc.dlists_label.c_str());
        return false;
    }
    if (target_row.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s uses baklst%d, but %s does not draw that plane.",
                 module_name, loc.baklst, loc.dlists_label.c_str());
        return false;
    }
    if (objlst_row.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no objlst fighter slot to place foreground after.",
                 loc.dlists_label.c_str());
        return false;
    }

    std::vector<std::string> rebuilt;
    rebuilt.reserve((size_t)(body_end - body_start + 1));
    rebuilt.insert(rebuilt.end(), passthrough.begin(), passthrough.end());
    rebuilt.insert(rebuilt.end(), back_planes.begin(), back_planes.end());
    rebuilt.insert(rebuilt.end(), pre_object_slots.begin(), pre_object_slots.end());
    rebuilt.push_back(objlst_row);
    rebuilt.push_back(target_row);
    rebuilt.insert(rebuilt.end(), post_object_slots.begin(), post_object_slots.end());
    rebuilt.push_back(terminator_row);

    bool changed = false;
    int original_count = body_end - body_start + 1;
    if (original_count != (int)rebuilt.size()) {
        changed = true;
    } else {
        for (int i = 0; i < original_count; i++) {
            if (lines[(size_t)(body_start + i)] != rebuilt[(size_t)i]) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s is already the only module foreground in %s.",
                 module_name, loc.dlists_label.c_str());
        return true;
    }

    lines.erase(lines.begin() + body_start, lines.begin() + body_end + 1);
    lines.insert(lines.begin() + body_start, rebuilt.begin(), rebuilt.end());

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_foreground_reset", backup, sizeof backup))
        return false;

    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Reset %s so only %s draws after players/shadows. Backup: %s.",
             loc.dlists_label.c_str(), module_name, backup);
    stage_set_toast("Reset foreground module order");
    return true;
}

bool stage_bgnd_set_module_over_fighters(const char *module_name, bool over_fighters)
{
    if (!module_name || !module_name[0])
        return false;

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
    if (loc.baklst < 1 || loc.baklst > 8 || loc.dlists_label.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no display-list baklst row to move.", module_name);
        return false;
    }

    int label_line = -1;
    if (!bgnd_find_exact_label_line(lines, loc.dlists_label.c_str(), &label_line)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Display list %s was not found.", loc.dlists_label.c_str());
        return false;
    }

    int row_line = -1;
    int objlst_line = -1;
    int shadow_line = -1;
    char want[32];
    snprintf(want, sizeof want, "baklst%d", loc.baklst);
    for (int i = label_line + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL))
            break;
        std::string tok = bgnd_directive_token(lines[(size_t)i], ".long");
        if (tok.empty())
            continue;
        if (bgnd_token_ieq(tok, want))
            row_line = i;
        if (bgnd_token_ieq(tok, "-2") && shadow_line < 0)
            shadow_line = i;
        if (bgnd_token_ieq(tok, "objlst") && objlst_line < 0)
            objlst_line = i;
        if (bgnd_token_ieq(tok, "0") || bgnd_token_ieq(tok, ">0"))
            break;
    }

    if (row_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s uses baklst%d, but %s does not draw that plane.",
                 module_name, loc.baklst, loc.dlists_label.c_str());
        return false;
    }
    if (objlst_line < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s has no objlst fighter slot to move around.",
                 loc.dlists_label.c_str());
        return false;
    }

    int behind_line = shadow_line >= 0 ? shadow_line : objlst_line;
    bool is_over = row_line > objlst_line;
    bool is_behind_players = row_line < behind_line;
    if (is_over == over_fighters) {
        if (over_fighters || is_behind_players) {
            snprintf(g_stage_start_status, sizeof g_stage_start_status,
                     "%s already draws %s fighters%s.",
                     module_name, over_fighters ? "over" : "behind",
                     over_fighters ? "" : "/shadows");
            return true;
        }
    }

    std::string row = lines[(size_t)row_line];
    lines.erase(lines.begin() + row_line);
    if (row_line < objlst_line)
        objlst_line--;
    if (row_line < shadow_line)
        shadow_line--;

    behind_line = shadow_line >= 0 ? shadow_line : objlst_line;
    int insert_at = over_fighters ? objlst_line + 1 : behind_line;
    if (insert_at < 0) insert_at = 0;
    if (insert_at > (int)lines.size()) insert_at = (int)lines.size();
    lines.insert(lines.begin() + insert_at, row);

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_objlst_order", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Moved %s to draw %s fighters%s in %s. Backup: %s.",
             module_name, over_fighters ? "over" : "behind",
             over_fighters ? "" : "/shadows",
             loc.dlists_label.c_str(), backup);
    stage_set_toast(over_fighters ? "Plane draws over fighters"
                                  : "Plane draws behind fighters");
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
             "Set its parallax from Game Preview -- it starts at whatever that plane's scroll row already holds.",
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

bool stage_bgnd_sync_runtime_offsets_from_bdb(void)
{
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    int changed = 0;
    int already = 0;
    int skipped = 0;
    for (int m = 0; m < g_bdb_num_modules; m++) {
        char name[64] = "";
        int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        if (sscanf(g_bdb_modules[m], "%63s %d %d %d %d", name, &x1, &x2, &y1, &y2) < 5 ||
            !name[0]) {
            skipped++;
            continue;
        }

        BgndModLoc loc;
        if (!bgnd_locate_module(lines, block_line, name, &loc)) {
            skipped++;
            continue;
        }

        std::string comment;
        if (loc.offset_line >= 0) {
            size_t semi = lines[(size_t)loc.offset_line].find(';');
            if (semi != std::string::npos)
                comment = lines[(size_t)loc.offset_line].substr(semi);
        }
        char wline[192];
        snprintf(wline, sizeof wline, "\t.word\t%d,%d%s%s", x1, y1,
                 comment.empty() ? "" : "\t\t", comment.c_str());
        if (loc.offset_line >= 0) {
            if (lines[(size_t)loc.offset_line] == wline) {
                already++;
            } else {
                lines[(size_t)loc.offset_line] = wline;
                changed++;
            }
        } else {
            lines.insert(lines.begin() + loc.bmod_line + 1, wline);
            changed++;
        }
    }

    if (changed <= 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Runtime offsets in %s already match current BDB source positions. "
                 "Skipped %d not-placed/bad module(s).",
                 block_label, skipped);
        return true;
    }

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_source_offset_sync", backup, sizeof backup))
        return false;

    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Synced %d runtime offset(s) in %s from current BDB module positions. "
             "%d already matched; %d skipped. Backup: %s.",
             changed, block_label, already, skipped, backup);
    stage_set_toast("Synced runtime offsets from BDB");
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

bool stage_bgnd_reset_all_module_parallax(float factor)
{
    char bgnd[640], block_label[96] = "";
    int block_line = -1;
    std::vector<std::string> lines;
    if (!bgnd_load_block(lines, block_label, sizeof block_label, &block_line, bgnd, sizeof bgnd))
        return false;

    std::string scroll_label;
    if (!bgnd_block_scroll_label(lines, block_line, &scroll_label) || scroll_label.empty()) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "No scroll table found for %s.", block_label);
        return false;
    }

    if (factor < 0.0f) factor = 0.0f;
    if (factor > 2.0f) factor = 2.0f;
    long rate = (long)(factor * 131072.0f + 0.5f);   /* playfield = 0x20000 */
    char val[32];
    if (rate == 0) snprintf(val, sizeof val, "0");
    else snprintf(val, sizeof val, "0%lxh", rate);

    int sl = -1;
    std::string lbl;
    for (int i = 0; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], &lbl) &&
            bgnd_token_ieq(lbl, scroll_label.c_str())) { sl = i; break; }
    }
    if (sl < 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Scroll table %s not found.", scroll_label.c_str());
        return false;
    }

    int row = 0, changed = 0;
    for (int i = sl + 1; i < (int)lines.size(); i++) {
        if (stage_start_asm_label_line(lines[(size_t)i], NULL)) break;
        if (bgnd_directive_token(lines[(size_t)i], ".long").empty()) continue;
        if (row >= 8) break;                         /* rows 0..7 = baklst8..1 */

        std::string comment;
        size_t semi = lines[(size_t)i].find(';');
        if (semi != std::string::npos)
            comment = lines[(size_t)i].substr(semi);
        char nl[192];
        snprintf(nl, sizeof nl, "\t.long\t%s%s%s",
                 val, comment.empty() ? "" : "\t\t", comment.c_str());
        if (lines[(size_t)i] != nl) {
            lines[(size_t)i] = nl;
            changed++;
        }
        row++;
    }

    if (row < 8) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Scroll table %s has only %d module row(s); expected 8.",
                 scroll_label.c_str(), row);
        return false;
    }
    if (changed == 0) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "All module parallax rows in %s are already %.2fx.",
                 scroll_label.c_str(), factor);
        return true;
    }

    char backup[640] = "";
    if (!bgnd_commit(bgnd, lines, ".pre_all_plane_parallax", backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Set all module parallax rows in %s to %.2fx. Backup: %s.",
             scroll_label.c_str(), factor, backup);
    stage_set_toast("Reset all module parallax");
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

bool stage_promote_draft_parallax_to_bgnd(void)
{
    char draft_path[640];
    if (!stage_draft_bgnd_path(draft_path, sizeof draft_path)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "No draft BGND.ASM for this stage to promote parallax from.");
        return false;
    }

    std::vector<std::string> draft_lines;
    if (!mk2_read_text_lines(draft_path, draft_lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not read %s.", draft_path);
        return false;
    }

    char draft_label[96] = "";
    int draft_line = -1;
    std::string draft_scroll;
    if (!bgnd_find_first_mod_block_with_scroll(draft_lines, draft_label, sizeof draft_label,
                                               &draft_line, &draft_scroll)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find a *_mod block with a scroll table in %s.", draft_path);
        return false;
    }

    std::vector<std::string> draft_rows;
    if (!bgnd_read_scroll_rows(draft_lines, draft_scroll, draft_rows,
                               g_stage_start_status, sizeof g_stage_start_status))
        return false;

    char real_path[640];
    if (!stage_real_bgnd_path(real_path, sizeof real_path)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not locate the real BGND.ASM to promote parallax into.");
        return false;
    }

    std::vector<std::string> real_lines;
    if (!mk2_read_text_lines(real_path, real_lines)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not read %s.", real_path);
        return false;
    }

    char real_label[96] = "";
    int real_line = -1;
    std::string real_scroll;
    if (!bgnd_find_existing_stage_block(real_lines, real_label, sizeof real_label,
                                        &real_line, &real_scroll)) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Could not find this stage's existing *_mod block in the real BGND.ASM.");
        return false;
    }

    bool changed = false;
    if (!bgnd_replace_scroll_rows(real_lines, real_scroll, draft_rows, &changed,
                                  g_stage_start_status, sizeof g_stage_start_status))
        return false;
    if (!changed) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "%s already matches draft parallax from %s.", real_label, draft_label);
        return true;
    }

    char backup[640] = "";
    if (!bgnd_commit(real_path, real_lines, ".pre_draft_parallax_promote",
                     backup, sizeof backup))
        return false;
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Promoted draft parallax %s/%s into %s/%s. Backup: %s.",
             draft_label, draft_scroll.c_str(), real_label, real_scroll.c_str(), backup);
    stage_set_toast("Promoted draft parallax");
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

    char promoted_path[680] = "";
    bool archived = mk2_copy_file_unique(draft_path, ".promoted",
                                         promoted_path, sizeof promoted_path);
    if (archived)
        remove(draft_path);
    bdd_invalidate_stage_module_cache();

    if (archived) {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Promoted %s into %s as table_o_mods entry %d. Backup: %s. Draft archived: %s.",
                 g_name, real_path, next_index, backup, promoted_path);
    } else {
        snprintf(g_stage_start_status, sizeof g_stage_start_status,
                 "Promoted %s into %s as table_o_mods entry %d. Backup: %s. Could not archive draft; it remains at %s.",
                 g_name, real_path, next_index, backup, draft_path);
    }
    stage_set_toast("Promoted stage to BGND.ASM");
    return true;
}

