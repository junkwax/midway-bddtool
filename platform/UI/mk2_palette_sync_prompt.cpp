#include "bg_editor_globals.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <string>
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
    std::vector<std::string> block;
    char line[256];
    snprintf(line, sizeof line, "%s:\t;PAL #%d", label.c_str(), pal_index);
    block.push_back(line);
    snprintf(line, sizeof line, "\t.word\t%d\t;pal size", g_pal_count[pal_index]);
    block.push_back(line);
    for (int start = 0; start < g_pal_count[pal_index]; start += 10) {
        std::string row = "\t.word ";
        int end = start + 10;
        if (end > g_pal_count[pal_index]) end = g_pal_count[pal_index];
        for (int i = start; i < end; i++) {
            if (i > start) row += ",";
            row += mk2_asm_hex(mk2_palette_word_from_argb(g_pals[pal_index][i]));
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

static bool mk2_palette_sync_current_matches(const char *bgnpal, const char *table_label,
                                             char *status, size_t statussz)
{
    if (status && statussz) status[0] = '\0';
    if (!bgnpal || !bgnpal[0] || !stage_file_exists(bgnpal)) return false;
    if (!table_label || !table_label[0] || g_n_pals <= 0) return false;

    std::vector<std::string> lines;
    if (!mk2_read_text_lines(bgnpal, lines)) return false;

    std::vector<std::string> used;
    std::vector<std::string> labels;
    for (int i = 0; i < g_n_pals; i++)
        labels.push_back(mk2_sanitize_asm_label(g_pal_name[i], used));

    for (int i = 0; i < g_n_pals; i++) {
        std::vector<std::string> block = mk2_palette_block_lines(labels[(size_t)i].c_str(), i);
        int existing = mk2_find_label_line(lines, labels[(size_t)i].c_str());
        if (existing < 0) return false;
        int end = mk2_section_end(lines, existing);
        if ((int)block.size() != end - existing) return false;
        for (int bi = 0; bi < (int)block.size(); bi++)
            if (lines[(size_t)(existing + bi)] != block[(size_t)bi])
                return false;
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
    std::vector<std::string> labels;
    for (int i = 0; i < g_n_pals; i++)
        labels.push_back(mk2_sanitize_asm_label(g_pal_name[i], used));

    bool changed = false;
    for (int i = 0; i < g_n_pals; i++) {
        table_start = mk2_find_label_line(lines, table_label);
        changed = mk2_replace_or_insert_palette_block(lines, labels[(size_t)i], i, &table_start) || changed;
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

static bool stage_start_find_bgnd_path(char *out, size_t outsz)
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
    snprintf(g_stage_start_status, sizeof g_stage_start_status,
             "Patched %s start camera to X=%d Y=%d. Backup: %s. Removed %d stale product(s).",
             block_label, g_stage_start_camera_x, g_stage_start_camera_y, backup, removed);
    stage_set_toast("Patched BGND start camera");
    return true;
}

