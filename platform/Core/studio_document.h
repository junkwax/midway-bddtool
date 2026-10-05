#pragma once

#include "Core/bdd_core.h"
#include "Core/bdd_metadata.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace studio {
struct OptimizationPlan;
struct PatternOptions;
struct PatternPlan;
struct VisibilityPlan;

using ObjectId = uint64_t;
struct Point {
    double x = 0, y = 0;
};
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(Point p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};
struct Viewport {
    Point pan;
    double zoom = 1;
    Point to_screen(Point p, Point origin) const;
    Point to_world(Point p, Point origin) const;
    void fit(Rect bounds, Rect viewport);
    void zoom_at(double value, Point mouse, Point origin);
};
struct AssetBank {
    BddCoreBdd data;
    std::vector<BddImageMetadata> metadata;
    std::vector<int> default_palettes;
    std::string original_metadata;
    size_t original_metadata_count = 0;
};
struct Plane {
    BddCoreModule source = {};
    std::string name;
    int x = 0, y = 0;
    double scroll = 1;
    int rank = 0;
    bool bound = false, hidden = false, locked = false;
};
struct Placement {
    ObjectId id = 0;
    BddCoreObject object = {};
    int plane = -1;
    bool hidden = false, locked = false;
    int runtime_dx = 0; // Source-coordinate move supplied by a custom stage generator.
};
struct State {
    std::shared_ptr<const AssetBank> assets;
    std::vector<Plane> planes;
    std::vector<Placement> objects;
    std::string name = "UNTITLED", header;
    int world_w = 800, world_h = 254, depth = 255;
    int start_x = 0, start_y = 0, ground = 230;
    bool has_bdb = true;
    std::string runtime_profile;
    uint64_t revision = 0;
};
// Read-only preparation used by folder import and its review UI.
bool make_raster_asset(const std::string &name, int width, int height, const uint8_t *rgba,
                       AssetBank &output, std::string &error);
std::shared_ptr<const AssetBank> prepare_asset_import(const State &state, const AssetBank &input,
                                                     bool reuse_palettes, std::string &error);
struct SceneItem {
    ObjectId id = 0;
    size_t object_index = 0, image_slot = 0;
    int palette = 0, rank = 0;
    Rect rect;
    bool hflip = false, vflip = false, locked = false;
};
enum class IssueGroup { References, Artwork, Load2, Layers, Camera };
struct Issue {
    std::string message;
    ObjectId object = 0;
    bool error = false;
    IssueGroup group = IssueGroup::References;
    std::string next_step;
    int image_slot = -1, palette = -1, plane = -1;
    bool has_camera = false;
    Point camera;
};
std::vector<SceneItem> scene_items(const State &state, Point camera = {}, bool source = false,
                                 int solo = -1);

// One owner and one history per document. No SDL, ImGui, global arrays, or assembly writes.
class Document {
  public:
    bool load(const std::string &path, std::string &error);
    bool save(const std::string &path, std::string &error, bool recovery = false);
    // Isolated export copy, sorted for LOAD2's X lookup; checks the serialized scene.
    bool save_game_sources(const std::string &path, std::string &report, std::string &error) const;
    static Document demo();
    static Document empty();
    const State &state() const { return state_; }
    const std::string &path() const { return path_; }
    const std::string &notice() const { return notice_; }
    bool dirty() const { return state_.revision != saved_revision_; }
    bool has_layout() const { return has_layout_; }
    const Placement *object(ObjectId id) const;
    const BddCoreImage *image(int image_id, size_t *slot = nullptr) const;
    std::vector<SceneItem> scene(Point camera = {}, bool source = false, int solo = -1) const;
    ObjectId pick(Point p, Point camera = {}, bool source = false, int solo = -1) const;
    Rect bounds(bool source = false) const;
    std::vector<Issue> validate() const;

    void begin(const std::string &label);
    bool preview_move(const std::vector<ObjectId> &ids, int dx, int dy);
    bool preview_plane(int plane, int dx, int dy);
    void commit();
    void cancel();
    bool transaction_active() const { return active_; }
    bool undo();
    bool redo();
    const char *undo_label() const;
    const char *redo_label() const;
    bool move(const std::vector<ObjectId> &ids, int dx, int dy);
    bool set_object(ObjectId id, int local_x, int local_y, int palette, bool flip_x, bool flip_y);
    bool assign_plane(const std::vector<ObjectId> &ids, int plane);
    std::vector<ObjectId> duplicate(const std::vector<ObjectId> &ids, int dx = 16, int dy = 0);
    bool erase(const std::vector<ObjectId> &ids);
    ObjectId place(int image_id, int palette, int plane, Point stage_position);
    bool set_plane(int index, const std::string &name, int x, int y, double scroll);
    bool set_plane_flags(int index, bool hidden, bool locked);
    bool reorder_plane(int index, int direction);
    int add_plane();
    bool set_start(int x, int y, int ground);
    bool import_rgba(const std::string &name, int width, int height, const uint8_t *rgba,
                     std::string &error, int &image_id);
    bool import_img(const std::string &path, const std::vector<std::string> &labels,
                    std::string &error, std::vector<int> &image_ids);
    bool import_assets(const AssetBank &input, bool reuse_palettes, std::string &error,
                       std::vector<int> &image_ids);
    bool set_palette(int palette, const BddCorePalette &colors, std::string &error);
    bool copy_palette_for_image(int image_id, int palette, std::string &error, int &new_palette);
    bool set_image_pixels(int image_id, const std::vector<uint8_t> &pixels, std::string &error);
    bool apply_optimization(const OptimizationPlan &plan, std::string &error);
    bool apply_pattern(const PatternPlan &plan, std::string &error);
    bool apply_visibility(const VisibilityPlan &plan, bool gameplay_contract_confirmed, std::string &error);
    // Read-only legacy runtime adapter seeds defaults once, before any editing.
    void seed_runtime(const std::vector<Plane> &planes, int start_x, int start_y, int ground);
    void seed_custom_runtime(const std::vector<Plane> &planes, int start_x, int start_y, int ground,
                             const std::vector<int> &source_dx, const std::string &profile);

  private:
    friend PatternPlan preview_pattern(const Document &, const PatternOptions &);
    struct Edit {
        std::string label;
        State before, after;
    };
    State state_, before_;
    std::vector<Edit> history_;
    size_t cursor_ = 0;
    uint64_t next_revision_ = 1, saved_revision_ = 0, next_id_ = 1;
    std::string path_, notice_, label_;
    bool active_ = false, changed_ = false, has_layout_ = false;
    bool editable(const Placement &p) const;
    void touch();
    BddCoreStage export_stage(std::vector<Plane> &planes) const;
};

} // namespace studio
