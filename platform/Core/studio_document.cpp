#include "Core/studio_document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace studio {
namespace fs = std::filesystem;
namespace {
bool selected(const std::vector<ObjectId> &ids, ObjectId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return s;
}
fs::path companion(fs::path p, const char *ext) {
    p.replace_extension(ext);
    if (fs::exists(p))
        return p;
    fs::path small = p;
    small.replace_extension(lower(ext));
    return fs::exists(small) ? small : p;
}
uint64_t file_hash(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    uint64_t h = 14695981039346656037ull;
    char c;
    while (in.get(c)) {
        h ^= (unsigned char)c;
        h *= 1099511628211ull;
    }
    return h;
}
bool overlaps(const BddCoreModule &a, const BddCoreModule &b) {
    return a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1;
}
std::string safe_name(std::string s) {
    for (char &c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            c = '_';
    if (s.empty())
        s = "UNTITLED";
    return s.substr(0, 63);
}
} // namespace

Point Viewport::to_screen(Point p, Point origin) const {
    return {origin.x + (p.x - pan.x) * zoom, origin.y + (p.y - pan.y) * zoom};
}
Point Viewport::to_world(Point p, Point origin) const {
    return {(p.x - origin.x) / zoom + pan.x, (p.y - origin.y) / zoom + pan.y};
}
void Viewport::fit(Rect b, Rect v) {
    zoom = std::clamp(std::min(std::max(1.0, v.w - 80) / std::max(1.0, b.w),
                               std::max(1.0, v.h - 80) / std::max(1.0, b.h)),
                      0.05, 16.0);
    pan = {b.x + b.w / 2 - v.w / (2 * zoom), b.y + b.h / 2 - v.h / (2 * zoom)};
}
void Viewport::zoom_at(double value, Point mouse, Point origin) {
    Point fixed = to_world(mouse, origin);
    zoom = std::clamp(value, 0.05, 16.0);
    pan = {fixed.x - (mouse.x - origin.x) / zoom, fixed.y - (mouse.y - origin.y) / zoom};
}

Document Document::empty() {
    Document d;
    d.state_.assets = std::make_shared<AssetBank>();
    Plane p;
    p.name = "Main layer";
    p.source.parsed = 1;
    std::snprintf(p.source.name, sizeof p.source.name, "PLANE1");
    p.source.x2 = 799;
    p.source.y2 = 253;
    d.state_.planes.push_back(p);
    d.state_.revision = d.next_revision_++;
    return d;
}

bool Document::load(const std::string &path, std::string &error) {
    try {
        fs::path input = fs::absolute(fs::u8path(path));
        if (!fs::is_regular_file(input)) {
            error = "File not found: " + input.u8string();
            return false;
        }
        std::string ext = lower(input.extension().u8string());
        if (ext != ".bdb" && ext != ".bdd") {
            error = "Choose a BDB or BDD file.";
            return false;
        }
        fs::path bdd = ext == ".bdd" ? input : companion(input, ".BDD");
        fs::path bdb = ext == ".bdb" ? input : companion(input, ".BDB");
        BddCoreStage loaded;
        if (!bdd_core_stage_load_bdd(&loaded, bdd.u8string().c_str())) {
            error = loaded.error;
            return false;
        }
        if (fs::exists(bdb) && !bdd_core_stage_load_bdb(&loaded, bdb.u8string().c_str())) {
            error = loaded.error;
            return false;
        }
        Document d;
        auto bank = std::make_shared<AssetBank>();
        bank->data = std::move(loaded.bdd);
        std::set<int> ids;
        for (const auto &im : bank->data.images) {
            if (!ids.insert(im.idx).second) {
                error = "Duplicate image IDs: repair the BDD before editing.";
                return false;
            }
            BddImageMetadata m = {};
            m.idx = im.idx;
            bank->metadata.push_back(m);
            bank->default_palettes.push_back(0);
        }
        bdd_metadata_load_records(bdd.u8string().c_str(), bank->metadata.data(),
                                  (int)bank->metadata.size());
        {
            std::ifstream meta(fs::path(bdd.u8string() + ".meta"), std::ios::binary);
            bank->original_metadata.assign(std::istreambuf_iterator<char>(meta), {});
            bank->original_metadata_count = bank->metadata.size();
        }
        for (size_t i = 0; i < bank->data.images.size(); i++)
            for (const auto &o : loaded.bdb.objects)
                if (o.ii == bank->data.images[i].idx) {
                    bank->default_palettes[i] = o.fl;
                    break;
                }
        d.state_.assets = bank;
        d.state_.has_bdb = loaded.has_bdb != 0;
        d.state_.name = loaded.bdb.name[0] ? loaded.bdb.name : input.stem().u8string();
        d.state_.header = loaded.bdb.header;
        d.state_.world_w = loaded.bdb.world_w;
        d.state_.world_h = loaded.bdb.world_h;
        d.state_.depth = loaded.bdb.max_depth;
        for (const auto &m : loaded.bdb.modules) {
            if (!m.parsed || m.x2 < m.x1 || m.y2 < m.y1) {
                error = "Invalid source module. Repair it in the specialist editor before opening.";
                return false;
            }
            Plane p;
            p.source = m;
            p.name = m.name;
            p.x = m.x1;
            p.y = m.y1;
            p.rank = (int)d.state_.planes.size();
            d.state_.planes.push_back(p);
        }
        for (const auto &o : loaded.bdb.objects) {
            Placement p;
            p.id = d.next_id_++;
            p.object = o;
            const auto *im = d.image(o.ii);
            if (im)
                p.plane = bdd_core_find_fitting_module(loaded.bdb.modules.data(),
                                                       (int)loaded.bdb.modules.size(), o.depth,
                                                       o.sy, im->w, im->h, nullptr);
            d.state_.objects.push_back(p);
        }
        d.path_ = (loaded.has_bdb ? bdb : bdd).u8string();
        fs::path layout = input;
        layout.replace_extension(".bddstudio");
        fs::path journal = input;
        journal.replace_extension(".bddstudio-saving");
        if (fs::exists(journal)) {
            error = "An interrupted save was found: " + journal.u8string() +
                    ". Restore the .studio-backup files listed there before opening.";
            return false;
        }
        if (fs::exists(layout)) {
            std::ifstream in(layout);
            std::string tag;
            int version = 0;
            uint64_t bh = 0, dh = 0;
            in >> tag >> version >> bh >> dh;
            if (tag != "BDDSTUDIO" || version != 1) {
                error = "Unsupported or damaged studio layout: " + layout.u8string();
                return false;
            }
            if (bh != (loaded.has_bdb ? file_hash(bdb) : 0) || dh != file_hash(bdd)) {
                d.notice_ = "Saved layout belongs to different BDB/BDD contents. Source positions "
                            "are shown; review before saving.";
            } else {
                while (in >> tag) {
                    if (tag == "camera")
                        in >> d.state_.start_x >> d.state_.start_y >> d.state_.ground;
                    else if (tag == "plane") {
                        size_t i = 0;
                        Plane p;
                        std::string source;
                        in >> i >> std::quoted(source) >> std::quoted(p.name) >> p.x >> p.y >>
                            p.scroll >> p.rank >> p.bound >> p.hidden >> p.locked;
                        if (i >= d.state_.planes.size() ||
                            source != d.state_.planes[i].source.name || !std::isfinite(p.scroll) ||
                            std::abs(p.scroll) > 16) {
                            error = "Invalid plane in studio layout.";
                            return false;
                        }
                        p.source = d.state_.planes[i].source;
                        d.state_.planes[i] = p;
                    } else if (tag == "asset") {
                        size_t i = 0;
                        int palette = 0;
                        in >> i >> palette;
                        if (i >= bank->default_palettes.size() || palette < 0 ||
                            palette >= (int)bank->data.palettes.size()) {
                            error = "Invalid asset palette in studio layout.";
                            return false;
                        }
                        bank->default_palettes[i] = palette;
                    } else if (tag == "object") {
                        size_t i = 0;
                        bool hidden = false, locked = false;
                        in >> i >> hidden >> locked;
                        if (i >= d.state_.objects.size()) {
                            error = "Invalid object in studio layout.";
                            return false;
                        }
                        d.state_.objects[i].hidden = hidden;
                        d.state_.objects[i].locked = locked;
                    } else {
                        error = "Unknown record in studio layout: " + tag;
                        return false;
                    }
                    if (!in) {
                        error = "Truncated studio layout.";
                        return false;
                    }
                }
                d.has_layout_ = true;
            }
        }
        *this = std::move(d);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

const Placement *Document::object(ObjectId id) const {
    for (const auto &p : state_.objects)
        if (p.id == id)
            return &p;
    return nullptr;
}
const BddCoreImage *Document::image(int id, size_t *slot) const {
    if (!state_.assets)
        return nullptr;
    for (size_t i = 0; i < state_.assets->data.images.size(); ++i)
        if (state_.assets->data.images[i].idx == id) {
            if (slot)
                *slot = i;
            return &state_.assets->data.images[i];
        }
    return nullptr;
}
bool Document::editable(const Placement &p) const {
    return !p.locked && !(p.plane >= 0 && state_.planes[p.plane].locked);
}
std::vector<SceneItem> Document::scene(Point camera, bool source, int solo) const {
    std::vector<SceneItem> result;
    for (size_t i = 0; i < state_.objects.size(); ++i) {
        const auto &p = state_.objects[i];
        const Plane *plane = p.plane >= 0 ? &state_.planes[p.plane] : nullptr;
        if (p.hidden || (plane && plane->hidden) || (solo >= 0 && p.plane != solo))
            continue;
        size_t slot = 0;
        const auto *im = image(p.object.ii, &slot);
        if (!im)
            continue;
        SceneItem item;
        item.id = p.id;
        item.object_index = i;
        item.image_slot = slot;
        item.palette = p.object.fl;
        item.locked = !editable(p);
        item.hflip = (p.object.wx & 0x10) != 0;
        item.vflip = (p.object.wx & 0x20) != 0;
        item.rank = plane ? plane->rank : 10000;
        double x = p.object.depth, y = p.object.sy;
        if (!source) {
            if (plane) {
                x += plane->x - plane->source.x1;
                y += plane->y - plane->source.y1;
            }
            x -= camera.x * (plane ? plane->scroll : 1.0);
            y -= camera.y;
        }
        item.rect = {x, y, (double)im->w, (double)im->h};
        result.push_back(item);
    }
    std::stable_sort(result.begin(), result.end(), [&](const SceneItem &a, const SceneItem &b) {
        if (!source && a.rank != b.rank)
            return a.rank < b.rank;
        const auto &x = state_.objects[a.object_index].object;
        const auto &y = state_.objects[b.object_index].object;
        if ((x.wx >> 8) != (y.wx >> 8))
            return (x.wx >> 8) < (y.wx >> 8);
        return x.order < y.order;
    });
    return result;
}
ObjectId Document::pick(Point pt, Point camera, bool source, int solo) const {
    auto items = scene(camera, source, solo);
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->locked || !it->rect.contains(pt))
            continue;
        const auto &im = state_.assets->data.images[it->image_slot];
        int x = (int)std::floor(pt.x - it->rect.x), y = (int)std::floor(pt.y - it->rect.y);
        if (it->hflip)
            x = im.w - 1 - x;
        if (it->vflip)
            y = im.h - 1 - y;
        if (im.pix[(size_t)y * im.w + x])
            return it->id;
    }
    return 0;
}
Rect Document::bounds(bool source) const {
    auto items = scene({}, source);
    if (items.empty())
        return {0, 0, 400, 254};
    double x1 = items[0].rect.x, y1 = items[0].rect.y, x2 = x1, y2 = y1;
    for (const auto &p : items) {
        x1 = std::min(x1, p.rect.x);
        y1 = std::min(y1, p.rect.y);
        x2 = std::max(x2, p.rect.x + p.rect.w);
        y2 = std::max(y2, p.rect.y + p.rect.h);
    }
    return {x1, y1, x2 - x1, y2 - y1};
}
void Document::seed_runtime(const std::vector<Plane> &planes, int x, int y, int ground) {
    if (has_layout_ || !notice_.empty() || dirty() || active_)
        return;
    for (auto &p : state_.planes)
        for (const auto &runtime : planes)
            if (lower(p.source.name) == lower(runtime.source.name)) {
                p.x = runtime.x;
                p.y = runtime.y;
                p.scroll = runtime.scroll;
                p.rank = runtime.rank;
                p.bound = true;
            }
    state_.start_x = x;
    state_.start_y = y;
    state_.ground = ground;
}
void Document::begin(const std::string &label) {
    if (active_)
        cancel();
    before_ = state_;
    label_ = label;
    active_ = true;
    changed_ = false;
}
void Document::touch() { changed_ = true; }
void Document::commit() {
    if (!active_)
        return;
    if (changed_) {
        state_.revision = next_revision_++;
        history_.resize(cursor_);
        history_.push_back({label_, before_, state_});
        cursor_++;
        if (history_.size() > 64) {
            history_.erase(history_.begin());
            cursor_--;
        }
    }
    before_ = {};
    active_ = changed_ = false;
}
void Document::cancel() {
    if (active_)
        state_ = before_;
    before_ = {};
    active_ = changed_ = false;
}
bool Document::undo() {
    cancel();
    if (!cursor_)
        return false;
    state_ = history_[--cursor_].before;
    return true;
}
bool Document::redo() {
    cancel();
    if (cursor_ >= history_.size())
        return false;
    state_ = history_[cursor_++].after;
    return true;
}
const char *Document::undo_label() const {
    return cursor_ ? history_[cursor_ - 1].label.c_str() : nullptr;
}
const char *Document::redo_label() const {
    return cursor_ < history_.size() ? history_[cursor_].label.c_str() : nullptr;
}
bool Document::preview_move(const std::vector<ObjectId> &ids, int dx, int dy) {
    if (!active_)
        return false;
    state_ = before_;
    changed_ = false;
    if (!dx && !dy)
        return false;
    for (auto &p : state_.objects)
        if (editable(p) && selected(ids, p.id)) {
            p.object.depth = std::clamp(p.object.depth + dx, -100000, 100000);
            p.object.sy = std::clamp(p.object.sy + dy, -100000, 100000);
            touch();
        }
    return changed_;
}
bool Document::preview_plane(int i, int dx, int dy) {
    if (!active_ || i < 0 || i >= (int)state_.planes.size())
        return false;
    state_ = before_;
    changed_ = false;
    auto &p = state_.planes[i];
    if (p.locked || (!dx && !dy))
        return false;
    p.x = std::clamp(p.x + dx, -100000, 100000);
    p.y = std::clamp(p.y + dy, -100000, 100000);
    touch();
    return true;
}
bool Document::move(const std::vector<ObjectId> &ids, int dx, int dy) {
    begin("Move artwork");
    bool ok = preview_move(ids, dx, dy);
    commit();
    return ok;
}
bool Document::set_object(ObjectId id, int x, int y, int palette, bool fx, bool fy) {
    const auto *cur = object(id);
    if (!cur || !editable(*cur) || palette < 0 ||
        palette >= (int)state_.assets->data.palettes.size())
        return false;
    begin("Edit artwork");
    for (auto &p : state_.objects)
        if (p.id == id) {
            const auto *plane = p.plane >= 0 ? &state_.planes[p.plane] : nullptr;
            auto &o = p.object;
            o.depth = std::clamp(x, -100000, 100000) + (plane ? plane->source.x1 : 0);
            o.sy = std::clamp(y, -100000, 100000) + (plane ? plane->source.y1 : 0);
            o.fl = palette;
            o.wx = (o.wx & ~0x30) | (fx ? 0x10 : 0) | (fy ? 0x20 : 0);
            touch();
        }
    commit();
    return true;
}
bool Document::assign_plane(const std::vector<ObjectId> &ids, int target) {
    if (target < 0 || target >= (int)state_.planes.size() || state_.planes[target].locked)
        return false;
    begin("Assign layer");
    const auto &dest = state_.planes[target];
    for (auto &p : state_.objects)
        if (selected(ids, p.id) && editable(p) && p.plane != target) {
            if (p.plane >= 0) {
                const auto &old = state_.planes[p.plane];
                p.object.depth += old.x - old.source.x1;
                p.object.sy += old.y - old.source.y1;
            }
            p.object.depth += dest.source.x1 - dest.x;
            p.object.sy += dest.source.y1 - dest.y;
            p.plane = target;
            touch();
        }
    bool ok = changed_;
    commit();
    return ok;
}
std::vector<ObjectId> Document::duplicate(const std::vector<ObjectId> &ids, int dx, int dy) {
    std::vector<Placement> copies;
    std::vector<ObjectId> result;
    int order = 0;
    for (const auto &p : state_.objects)
        order = std::max(order, p.object.order + 1);
    for (auto p : state_.objects)
        if (selected(ids, p.id) && editable(p)) {
            if (state_.objects.size() + copies.size() >= BDD_CORE_MAX_OBJECTS)
                break;
            p.id = next_id_++;
            p.object.depth += dx;
            p.object.sy += dy;
            p.object.order = order++;
            copies.push_back(p);
            result.push_back(p.id);
        }
    if (!copies.empty()) {
        begin("Duplicate artwork");
        state_.objects.insert(state_.objects.end(), copies.begin(), copies.end());
        touch();
        commit();
    }
    return result;
}
bool Document::erase(const std::vector<ObjectId> &ids) {
    begin("Delete artwork");
    size_t n = state_.objects.size();
    state_.objects.erase(
        std::remove_if(state_.objects.begin(), state_.objects.end(),
                       [&](const Placement &p) { return editable(p) && selected(ids, p.id); }),
        state_.objects.end());
    if (n != state_.objects.size())
        touch();
    bool ok = changed_;
    commit();
    return ok;
}
ObjectId Document::place(int image_id, int palette, int plane, Point pos) {
    if (!image(image_id) || plane < 0 || plane >= (int)state_.planes.size() ||
        state_.planes[plane].locked || state_.objects.size() >= BDD_CORE_MAX_OBJECTS)
        return 0;
    if (palette < 0 || palette >= (int)state_.assets->data.palettes.size())
        return 0;
    begin("Place artwork");
    const auto &layer = state_.planes[plane];
    Placement p;
    p.id = next_id_++;
    p.plane = plane;
    p.object.ii = image_id;
    p.object.fl = palette;
    p.object.wx = 0x4000;
    p.object.depth = (int)std::round(pos.x) - layer.x + layer.source.x1;
    p.object.sy = (int)std::round(pos.y) - layer.y + layer.source.y1;
    for (const auto &o : state_.objects)
        p.object.order = std::max(p.object.order, o.object.order + 1);
    state_.objects.push_back(p);
    state_.has_bdb = true;
    touch();
    commit();
    return p.id;
}
bool Document::set_plane(int i, const std::string &name, int x, int y, double scroll) {
    if (i < 0 || i >= (int)state_.planes.size() || state_.planes[i].locked ||
        !std::isfinite(scroll))
        return false;
    begin("Edit layer");
    auto &p = state_.planes[i];
    p.name = name.substr(0, 63);
    p.x = std::clamp(x, -100000, 100000);
    p.y = std::clamp(y, -100000, 100000);
    p.scroll = std::clamp(scroll, -16.0, 16.0);
    touch();
    commit();
    return true;
}
bool Document::set_plane_flags(int i, bool hidden, bool locked) {
    if (i < 0 || i >= (int)state_.planes.size())
        return false;
    begin("Layer visibility / lock");
    state_.planes[i].hidden = hidden;
    state_.planes[i].locked = locked;
    touch();
    commit();
    return true;
}
bool Document::reorder_plane(int i, int direction) {
    if (i < 0 || i >= (int)state_.planes.size() || state_.planes[i].locked)
        return false;
    std::vector<int> order;
    for (size_t j = 0; j < state_.planes.size(); j++)
        order.push_back((int)j);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return state_.planes[a].rank < state_.planes[b].rank; });
    int from = (int)(std::find(order.begin(), order.end(), i) - order.begin()),
        to = from + direction;
    if (to < 0 || to >= (int)order.size())
        return false;
    begin("Reorder layer");
    std::swap(order[from], order[to]);
    for (size_t j = 0; j < order.size(); j++)
        state_.planes[order[j]].rank = (int)j;
    touch();
    commit();
    return true;
}
int Document::add_plane() {
    if (state_.planes.size() >= BDD_CORE_MAX_MODULES)
        return -1;
    begin("Add layer");
    Plane p;
    int i = (int)state_.planes.size();
    p.rank = i;
    std::set<std::string> names;
    for (auto &old : state_.planes)
        names.insert(lower(old.source.name));
    int n = i + 1;
    do {
        std::snprintf(p.source.name, sizeof p.source.name, "LAYER%d", n++);
    } while (names.count(lower(p.source.name)));
    p.name = p.source.name;
    p.source.parsed = 1;
    p.source.x2 = 399;
    p.source.y1 = (i + 1) * 512;
    p.source.y2 = p.source.y1 + 253;
    state_.planes.push_back(p);
    state_.has_bdb = true;
    touch();
    commit();
    return i;
}
bool Document::set_start(int x, int y, int ground) {
    begin("Set camera start");
    state_.start_x = x;
    state_.start_y = y;
    state_.ground = ground;
    touch();
    commit();
    return true;
}

bool Document::import_rgba(const std::string &name, int w, int h, const uint8_t *rgba,
                           std::string &error, int &id) {
    if (!rgba || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        error = "Image dimensions must be between 1 and 4096 pixels.";
        return false;
    }
    if (state_.assets->data.images.size() >= BDD_CORE_MAX_IMAGES ||
        state_.assets->data.palettes.size() >= BDD_CORE_MAX_PALS) {
        error = "Image or palette limit reached.";
        return false;
    }
    std::map<uint16_t, size_t> histogram;
    for (size_t i = 0; i < (size_t)w * h; i++)
        if (rgba[i * 4 + 3] >= 128) {
            uint32_t c =
                0xff000000u | (rgba[i * 4] << 16) | (rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
            histogram[bdd_core_argb_to_rgb555(c)]++;
        }
    // Exact RGB555 import. Color-reducing tools remain explicit in the specialist editor.
    if (histogram.size() > 255) {
        error = "This image uses more than 255 RGB555 colors. Reduce its palette in the specialist "
                "editor before importing.";
        return false;
    }
    auto bank = std::make_shared<AssetBank>(*state_.assets);
    BddCoreImage im;
    im.idx = 0;
    im.w = w;
    im.h = h;
    im.flags = 0;
    im.pix.resize((size_t)w * h);
    for (const auto &existing : bank->data.images)
        im.idx = std::max(im.idx, existing.idx + 1);
    if (im.idx > 65535) {
        error = "No free image ID.";
        return false;
    }
    BddCorePalette pal = {};
    std::snprintf(pal.name, sizeof pal.name, "%s", safe_name(name).c_str());
    pal.count = 1;
    std::map<uint16_t, int> indices;
    for (auto kv : histogram) {
        int n = pal.count++;
        indices[kv.first] = n;
        pal.rgb555[n] = kv.first;
        pal.argb[n] = bdd_core_rgb555_to_argb(kv.first);
    }
    for (size_t i = 0; i < im.pix.size(); i++)
        if (rgba[i * 4 + 3] >= 128) {
            uint32_t c =
                0xff000000u | (rgba[i * 4] << 16) | (rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
            im.pix[i] = (uint8_t)indices[bdd_core_argb_to_rgb555(c)];
        }
    id = im.idx;
    BddImageMetadata meta = {};
    meta.idx = id;
    std::snprintf(meta.label, sizeof meta.label, "%s", name.c_str());
    bank->data.images.push_back(std::move(im));
    bank->data.palettes.push_back(pal);
    bank->metadata.push_back(meta);
    bank->default_palettes.push_back((int)bank->data.palettes.size() - 1);
    begin("Import image");
    state_.assets = bank;
    touch();
    commit();
    return true;
}

BddCoreStage Document::export_stage(std::vector<Plane> &planes) const {
    BddCoreStage out;
    out.has_bdb = state_.has_bdb;
    out.has_bdd = 1;
    out.bdd = state_.assets->data;
    planes = state_.planes;
    bool pack = false;
    for (size_t i = 0; i < planes.size(); i++) {
        for (size_t j = i + 1; j < planes.size(); j++)
            if (overlaps(planes[i].source, planes[j].source))
                pack = true;
        for (const auto &p : state_.objects)
            if (p.plane == (int)i) {
                auto *im = image(p.object.ii);
                if (im && !bdd_core_object_fits_module(&p.object, &planes[i].source, im->w, im->h))
                    pack = true;
            }
    }
    std::vector<Point> delta(planes.size());
    if (pack) {
        int shelf = 0;
        for (size_t i = 0; i < planes.size(); i++) {
            auto &p = planes[i];
            int x1 = 0, y1 = 0, x2 = 1, y2 = 1;
            bool first = true;
            for (const auto &o : state_.objects)
                if (o.plane == (int)i) {
                    auto *im = image(o.object.ii);
                    if (!im)
                        continue;
                    int x = o.object.depth - p.source.x1, y = o.object.sy - p.source.y1;
                    if (first) {
                        x1 = x;
                        y1 = y;
                        x2 = x + im->w;
                        y2 = y + im->h;
                        first = false;
                    } else {
                        x1 = std::min(x1, x);
                        y1 = std::min(y1, y);
                        x2 = std::max(x2, x + im->w);
                        y2 = std::max(y2, y + im->h);
                    }
                }
            delta[i] = {(double)(-p.source.x1 - x1), (double)(shelf - p.source.y1 - y1)};
            p.x += x1;
            p.y += y1;
            p.source.x1 = 0;
            p.source.x2 = x2 - x1 - 1;
            p.source.y1 = shelf;
            p.source.y2 = shelf + y2 - y1 - 1;
            shelf = p.source.y2 + 65;
        }
    }
    for (auto &p : planes) {
        std::snprintf(p.source.line, sizeof p.source.line, "%s %d %d %d %d", p.source.name,
                      p.source.x1, p.source.x2, p.source.y1, p.source.y2);
        out.bdb.modules.push_back(p.source);
    }
    for (const auto &p : state_.objects) {
        auto o = p.object;
        if (p.plane >= 0) {
            o.depth += (int)delta[p.plane].x;
            o.sy += (int)delta[p.plane].y;
        }
        out.bdb.objects.push_back(o);
    }
    char header[256];
    std::snprintf(header, sizeof header, "%s %d %d %d %d %d %d", safe_name(state_.name).c_str(),
                  std::max(400, state_.world_w), std::max(254, state_.world_h), state_.depth,
                  (int)out.bdb.modules.size(), (int)out.bdd.palettes.size(),
                  (int)out.bdb.objects.size());
    out.bdb.header = header;
    return out;
}

bool Document::save(const std::string &path, std::string &error, bool recovery) {
    if (active_) {
        error = "Finish or cancel the current edit before saving.";
        return false;
    }
    try {
        fs::path base = fs::absolute(fs::u8path(path));
        fs::path bdb = companion(base, ".BDB"), bdd = companion(base, ".BDD");
        fs::path layout = base;
        layout.replace_extension(".bddstudio");
        fs::path journal = base;
        journal.replace_extension(".bddstudio-saving");
        if (fs::exists(journal)) {
            error = "Unfinished save exists: " + journal.u8string();
            return false;
        }
        if (!state_.has_bdb && fs::exists(bdb)) {
            error = "A BDB already uses that filename. Choose a different name for this standalone "
                    "BDD.";
            return false;
        }
        std::vector<Plane> planes;
        auto output = export_stage(planes);
        // Unassigned source objects may be preserved, but repacking must never change their
        // ownership silently.
        for (size_t i = 0; i < state_.objects.size(); i++) {
            const auto &p = state_.objects[i];
            const auto *im = image(p.object.ii);
            if (!im) {
                error = "A placement references a missing image.";
                return false;
            }
            if (p.object.fl < 0 || p.object.fl >= (int)state_.assets->data.palettes.size()) {
                error = "A placement references a missing palette.";
                return false;
            }
            int owner = bdd_core_find_fitting_module(
                output.bdb.modules.data(), (int)output.bdb.modules.size(),
                output.bdb.objects[i].depth, output.bdb.objects[i].sy, im->w, im->h, nullptr);
            if (owner != p.plane) {
                error = "Assign unassigned artwork to a layer before saving this layout.";
                return false;
            }
        }
        struct File {
            fs::path target, temp, backup;
            bool existed = false, installed = false;
        };
        std::vector<File> files;
        auto add = [&](fs::path target) {
            files.push_back({target, fs::path(target.u8string() + ".studio-tmp"),
                             fs::path(target.u8string() + ".studio-backup"), fs::exists(target),
                             false});
        };
        if (output.has_bdb)
            add(bdb);
        add(bdd);
        add(layout);
        // Preserve opaque metadata records; append records only for newly imported images.
        add(fs::path(bdd.u8string() + ".meta"));
        for (const auto &f : files) {
            if (!fs::is_directory(f.target.parent_path())) {
                error = "Save folder does not exist.";
                return false;
            }
            if (f.existed &&
                (fs::status(f.target).permissions() & fs::perms::owner_write) == fs::perms::none) {
                error = "Read-only save target: " + f.target.u8string();
                return false;
            }
        }
        BddCoreSaveResult result;
        size_t index = 0;
        if (output.has_bdb) {
            std::vector<const char *> lines;
            for (const auto &p : output.bdb.modules)
                lines.push_back(p.line);
            if (!bdd_core_save_bdb(files[index++].temp.u8string().c_str(),
                                   output.bdb.header.c_str(), lines.data(), (int)lines.size(),
                                   output.bdb.objects.data(), (int)output.bdb.objects.size(),
                                   &result)) {
                error = result.error;
                return false;
            }
        }
        fs::path temp_bdd = files[index++].temp;
        if (!bdd_core_save_bdd(temp_bdd.u8string().c_str(), output.bdd.images.data(),
                               (int)output.bdd.images.size(), output.bdd.palettes.data(),
                               (int)output.bdd.palettes.size(), &result)) {
            error = result.error;
            return false;
        }
        {
            std::ofstream out(files[index++].temp, std::ios::trunc);
            out << "BDDSTUDIO 1 " << (output.has_bdb ? file_hash(files[0].temp) : 0) << ' '
                << file_hash(temp_bdd) << '\n';
            out << "camera " << state_.start_x << ' ' << state_.start_y << ' ' << state_.ground
                << '\n';
            for (size_t i = 0; i < planes.size(); i++) {
                const auto &p = planes[i];
                out << "plane " << i << ' ' << std::quoted(p.source.name) << ' '
                    << std::quoted(p.name) << ' ' << p.x << ' ' << p.y << ' '
                    << std::setprecision(12) << p.scroll << ' ' << p.rank << ' ' << p.bound << ' '
                    << p.hidden << ' ' << p.locked << '\n';
            }
            for (size_t i = 0; i < state_.assets->default_palettes.size(); i++)
                out << "asset " << i << ' ' << state_.assets->default_palettes[i] << '\n';
            for (size_t i = 0; i < state_.objects.size(); i++)
                out << "object " << i << ' ' << state_.objects[i].hidden << ' '
                    << state_.objects[i].locked << '\n';
            out.close();
            if (!out) {
                error = "Could not write studio layout.";
                return false;
            }
        }
        {
            std::ofstream out(files[index].temp, std::ios::binary | std::ios::trunc);
            if (state_.assets->original_metadata.empty())
                out << "# bddview image metadata v1\n";
            out << state_.assets->original_metadata;
            if (!state_.assets->original_metadata.empty() &&
                state_.assets->original_metadata.back() != '\n')
                out << '\n';
            for (size_t mi = state_.assets->original_metadata_count;
                 mi < state_.assets->metadata.size(); mi++) {
                auto m = state_.assets->metadata[mi];
                for (char *text : {m.label, m.source})
                    for (char *c = text; *c; c++)
                        if (*c == '\t' || *c == '\n' || *c == '\r')
                            *c = ' ';
                out << "IMG\t" << std::hex << m.idx << std::dec << '\t' << m.label << '\t'
                    << m.source << '\t' << m.anix << '\t' << m.aniy << '\t' << m.anix2 << '\t'
                    << m.aniy2 << '\t' << m.aniz2 << '\t' << m.frm << '\t' << m.opals << '\t'
                    << m.pttblnum << '\t' << m.lod_ref << '\n';
            }
            out.close();
            if (!out) {
                error = "Could not write image metadata.";
                return false;
            }
        }
        // Prepare every file and every backup before replacing the first target.
        for (auto &f : files)
            if (f.existed)
                fs::copy_file(f.target, f.backup, fs::copy_options::overwrite_existing);
        {
            std::ofstream out(journal);
            out << "BDD Studio interrupted-save recovery. Restore each existing target from its "
                   "backup.\n";
            for (const auto &f : files)
                out << std::quoted(f.target.u8string()) << ' ' << f.existed << ' '
                    << std::quoted(f.backup.u8string()) << '\n';
            out.close();
            if (!out) {
                error = "Could not create save recovery journal.";
                return false;
            }
        }
        try {
            for (auto &f : files) {
                // portable rename requires a vacant destination on Windows; backups and journal
                // cover interruption.
                if (f.existed)
                    fs::remove(f.target);
                f.installed = true;
                fs::rename(f.temp, f.target);
            }
            fs::remove(journal);
        } catch (...) {
            bool restored = true;
            for (auto &f : files)
                if (f.installed) {
                    std::error_code ec;
                    if (f.existed)
                        fs::copy_file(f.backup, f.target, fs::copy_options::overwrite_existing, ec);
                    else
                        fs::remove(f.target, ec);
                    restored = restored && !ec;
                }
            if (restored) {
                std::error_code ec;
                fs::remove(journal, ec);
            }
            throw;
        }
        if (!recovery) {
            path_ = (output.has_bdb ? bdb : bdd).u8string();
            saved_revision_ = state_.revision;
            has_layout_ = true;
            notice_.clear();
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

std::vector<Issue> Document::validate() const {
    std::vector<Issue> issues;
    for (const auto &p : state_.objects) {
        auto *im = image(p.object.ii);
        if (!im) {
            issues.push_back({"Missing image", p.id, true});
            continue;
        }
        if (p.plane < 0)
            issues.push_back({"Artwork is not assigned to a layer", p.id, true});
        if (p.object.fl < 0 || p.object.fl >= (int)state_.assets->data.palettes.size())
            issues.push_back({"Palette is missing", p.id, true});
        if (im->w > BDD_CORE_MK2_RUNTIME_WIDEST_BLOCK)
            issues.push_back({"Image exceeds MK2's 250-pixel block width", p.id, false});
        if (im->w % 4)
            issues.push_back({"Image width is not a multiple of 4 for LOAD2", p.id, false});
    }
    for (const auto &p : state_.planes)
        if (!p.bound)
            issues.push_back(
                {p.name + ": no game-plane binding; composition is estimated", 0, false});
    return issues;
}

Document Document::demo() {
    Document d = empty();
    auto bank = std::make_shared<AssetBank>();
    BddCorePalette pal = {};
    std::snprintf(pal.name, sizeof pal.name, "COURTYARD");
    pal.count = 12;
    const uint32_t colors[] = {0,          0xff18232e, 0xff243744, 0xff344958,
                               0xff52636b, 0xff8b9691, 0xffcfbf92, 0xff997755,
                               0xff5a5948, 0xff425953, 0xff2b403e, 0xffc19157};
    for (int i = 0; i < 12; i++) {
        pal.rgb555[i] = bdd_core_argb_to_rgb555(colors[i]);
        pal.argb[i] = bdd_core_rgb555_to_argb(pal.rgb555[i]);
    }
    bank->data.palettes.push_back(pal);
    const int widths[] = {128, 48, 128}, heights[] = {160, 144, 48};
    const char *names[] = {"Stone wall", "Pillar", "Ground"};
    for (int k = 0; k < 3; k++) {
        BddCoreImage im;
        im.idx = k + 1;
        im.w = widths[k];
        im.h = heights[k];
        im.flags = 0;
        im.pix.resize((size_t)im.w * im.h);
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                int c = 0;
                if (k == 0)
                    c = (y % 24 == 0 || (x + (y / 24 % 2) * 32) % 64 == 0)
                            ? 1
                            : 2 + ((x / 64 + y / 24) % 2);
                if (k == 1) {
                    if (y < 12 || y > 132)
                        c = (x > 1 && x < 46) ? 5 : 0;
                    else if (x > 8 && x < 40)
                        c = x < 14 ? 6 : (x > 33 ? 3 : 4);
                }
                if (k == 2)
                    c = y < 4 ? 9 : (y < 8 ? 10 : ((x + y * 3) % 37 < 3 ? 7 : 8));
                im.pix[(size_t)y * im.w + x] = (uint8_t)c;
            }
        bank->data.images.push_back(im);
        BddImageMetadata m = {};
        m.idx = im.idx;
        std::snprintf(m.label, sizeof m.label, "%s", names[k]);
        bank->metadata.push_back(m);
        bank->default_palettes.push_back(0);
    }
    d.state_.assets = bank;
    d.state_.name = "COURTYARD";
    d.state_.planes.clear();
    for (int i = 0; i < 3; i++) {
        Plane p;
        p.name = i == 0 ? "Background" : i == 1 ? "Pillars" : "Ground";
        p.rank = i;
        p.scroll = i == 0 ? 0.5 : 1;
        p.source.parsed = 1;
        p.source.x2 = 1023;
        p.source.y1 = i * 512;
        p.source.y2 = p.source.y1 + 253;
        std::snprintf(p.source.name, sizeof p.source.name, "PLANE%d", i + 1);
        d.state_.planes.push_back(p);
    }
    for (int layer = 0; layer < 3; layer++)
        for (int i = 0; i < (layer == 1 ? 4 : 7); i++) {
            Placement p;
            p.id = d.next_id_++;
            p.plane = layer;
            p.object.wx = 0x4000;
            p.object.ii = layer + 1;
            p.object.fl = 0;
            p.object.order = (int)d.state_.objects.size();
            p.object.depth = i * (layer == 1 ? 210 : 128) + (layer == 1 ? 24 : 0);
            p.object.sy = layer * 512 + (layer == 0 ? 48 : layer == 1 ? 76 : 216);
            d.state_.objects.push_back(p);
        }
    d.state_.start_x = 200;
    d.state_.ground = 224;
    return d;
}
} // namespace studio
