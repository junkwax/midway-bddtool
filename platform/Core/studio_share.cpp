#include "Core/studio_share.h"
#include "Core/zip_writer.h"
#include "libs/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace studio {
namespace {
namespace fs = std::filesystem;
void require(bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); }
std::string markdown(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '\n' || c == '\r') out += " ";
        else { if (std::string("\\`*_{}[]()|#!").find(c) != std::string::npos) out += '\\'; out += c; }
    }
    return out;
}
std::string url_encode(const std::string &text) {
    std::string out; const char *hex = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
void png(const fs::path &path, int w, int h, const std::vector<uint8_t> &pixels) {
    require(stbi_write_png(path.u8string().c_str(), w, h, 4, pixels.data(), w * 4) != 0,
            "Cannot write preview: " + path.u8string());
}
void render(const State &s, Point camera, Rect bounds, const fs::path &path) {
    // A bounded overview can be downscaled; the camera image remains exactly 400x254.
    double scale = std::min({1.0, 2048.0 / std::max(1.0, bounds.w), 1024.0 / std::max(1.0, bounds.h)});
    int w = std::max(1, (int)std::ceil(bounds.w * scale)), h = std::max(1, (int)std::ceil(bounds.h * scale));
    std::vector<uint8_t> rgba(size_t(w) * h * 4, 0);
    uint64_t work = 0;
    for (const auto &item : scene_items(s, camera)) {
        const auto &im = s.assets->data.images[item.image_slot];
        const auto &pal = s.assets->data.palettes[item.palette];
        int x0 = (int)std::clamp(std::floor((item.rect.x - bounds.x) * scale), 0.0, double(w));
        int y0 = (int)std::clamp(std::floor((item.rect.y - bounds.y) * scale), 0.0, double(h));
        int x1 = (int)std::clamp(std::ceil((item.rect.x + im.w - bounds.x) * scale), 0.0, double(w));
        int y1 = (int)std::clamp(std::ceil((item.rect.y + im.h - bounds.y) * scale), 0.0, double(h));
        work += uint64_t(std::max(0, x1 - x0)) * std::max(0, y1 - y0);
        require(work <= 128000000, "Preview exceeds the 128-million-pixel compositing limit.");
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            int sx = (int)std::floor(bounds.x + (x + .5) / scale - item.rect.x);
            int sy = (int)std::floor(bounds.y + (y + .5) / scale - item.rect.y);
            if (sx < 0 || sy < 0 || sx >= im.w || sy >= im.h) continue;
            if (item.hflip) sx = im.w - 1 - sx;
            if (item.vflip) sy = im.h - 1 - sy;
            int index = im.pix[size_t(sy) * im.w + sx]; if (!index) continue;
            uint32_t color = pal.argb[index]; size_t p = (size_t(y) * w + x) * 4;
            rgba[p] = uint8_t(color >> 16); rgba[p+1] = uint8_t(color >> 8); rgba[p+2] = uint8_t(color); rgba[p+3] = 255;
        }
    }
    png(path, w, h, rgba);
}
}
ShareBundle build_stage_share(const Document &document, const ShareCredits &credits, const std::string &new_folder) {
    ShareBundle out; out.revision = document.state().revision; out.assets = document.state().assets;
    out.credits = credits;
    try {
        const auto &state = document.state();
        require(state.has_bdb && state.assets && !state.objects.empty(), "Open a stage with placed artwork before sharing.");
        require(!credits.author.empty() && !credits.license.empty(), "Add author and use/license information before building the bundle.");
        const std::string name = credits.page_name.empty() ? state.name : credits.page_name;
        require(!name.empty() && name.size() <= 63 && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }), "Bundle/wiki name must be 1-63 letters, digits or underscores.");
        auto folded = name;
        for (auto &c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        require(folded != "home" && folded != "_sidebar" && folded != "_footer", "This stage name is reserved by the wiki.");
        auto issues = document.validate();
        for (const auto &issue : issues)
            require(!(issue.error && (issue.group == IssueGroup::References || issue.group == IssueGroup::Artwork)),
                    "Resolve before sharing: " + issue.message);
        size_t pixels = 0;
        for (const auto &im : state.assets->data.images) pixels += im.pix.size();
        require(pixels <= 16000000, "Share bundle exceeds the 16-million-pixel artwork limit.");
        for (const auto &plane : state.planes)
            require(std::isfinite(plane.scroll) && std::abs(plane.scroll) <= 100, "Invalid layer parallax for previews.");
        const fs::path folder = fs::absolute(fs::u8path(new_folder));
        require(!new_folder.empty() && fs::is_directory(folder.parent_path()), "Choose an existing parent folder.");
        require(fs::create_directory(folder), "Bundle folder already exists. Choose a new folder; existing files are never overwritten.");
        out.folder = folder.u8string();
        Document snapshot = document;
        std::string error;
        if (!snapshot.save((folder / (name + ".BDB")).u8string(), error))
            throw std::runtime_error("Share snapshot save failed: " + error);
        // Previews include every saved placement, regardless of editor-only visibility.
        State rendered = state;
        for (auto &p : rendered.objects) p.hidden = false;
        for (auto &p : rendered.planes) p.hidden = false;
        auto items = scene_items(rendered);
        require(!items.empty(), "No renderable placements in the stage.");
        Rect bounds = items.front().rect;
        for (const auto &item : items) {
            double right = std::max(bounds.x + bounds.w, item.rect.x + item.rect.w);
            double bottom = std::max(bounds.y + bounds.h, item.rect.y + item.rect.h);
            bounds.x = std::min(bounds.x, item.rect.x); bounds.y = std::min(bounds.y, item.rect.y);
            bounds.w = right - bounds.x; bounds.h = bottom - bounds.y;
        }
        render(rendered, {}, bounds, folder / "arena_layout.png");
        render(rendered, {double(state.start_x), double(state.start_y)}, {0, 0, 400, 254}, folder / "arena_game.png");
        fs::create_directory(folder / "props");
        std::ostringstream md;
        md << "# " << name << "\n\nSource stage: " << markdown(state.name) << ". Bundle filenames do not rename internal stage/module identifiers.\n\n" << markdown(credits.description) << "\n\n## Credits\n\n"
           << "| | |\n|---|---|\n| **Author** | " << markdown(credits.author) << " |\n"
           << "| **Use** | " << markdown(credits.license) << " |\n"
           << "| **Game testing** | " << markdown(credits.tested.empty() ? "Not reported" : credits.tested) << " |\n\n"
           << "Credits / sources: " << markdown(credits.sources) << "\n\n## Download\n\n";
        for (const auto &ext : {".BDB", ".BDD", ".bddstudio", ".BDD.meta"})
            md << "- [" << name << ext << "](" << name << ext << ")\n";
        md << "\nKeep all four files together to preserve editor layout and metadata.\n\n## Arena\n\n"
           << "Static authoring previews, including editor-hidden placements. Runtime animations, actors and compiled ROM behavior are not pictured or verified.\n\n"
           << "![Stage layout](arena_layout.png)\n\n![Start camera, 400x254](arena_game.png)\n\n## Props\n\n"
           << "All BDD images, using their default palettes; placed variants remain in the stage files.\n\n";
        for (size_t i = 0; i < state.assets->data.images.size(); ++i) {
            const auto &im = state.assets->data.images[i];
            int palette = state.assets->default_palettes[i];
            const auto &pal = state.assets->data.palettes[palette];
            std::vector<uint8_t> rgba(im.pix.size() * 4);
            require(bdd_core_indexed_to_rgba(im.pix.data(), im.w, im.h, pal.argb, pal.count, rgba.data(), rgba.size()) != 0, "Cannot render prop.");
            auto filename = "props/" + std::to_string(im.idx) + ".png";
            png(folder / fs::u8path(filename), im.w, im.h, rgba);
            md << "![](" << filename << ") Image " << im.idx << " (" << im.w << "x" << im.h << ")\n\n";
        }
        md << "## Authoring data\n\n| Metric | Count |\n|---|---:|\n| Modules (planes) | " << state.planes.size()
           << " |\n| Blocks | " << state.objects.size() << " |\n| Images | " << state.assets->data.images.size()
           << " |\n| Palettes | " << state.assets->data.palettes.size() << " |\n\n## How to enable\n\n"
           << "Open the BDB in bddtool with its companion files beside it. Use Build & Check to prepare and review an export against your target checkout. The archive contains authoring sources, not an installed or verified ROM. Game export prepares runtime X-order separately.\n\n"
           << "Runtime animation IMG files and game assembly dependencies are not included. Custom generators and unbound layers need their matching game setup; do not substitute guessed bindings.\n\n"
           << "Start camera: " << state.start_x << ", " << state.start_y << "; ground: " << state.ground << ".\n\n"
           << "| Layer | X | Y | Parallax | Runtime binding |\n|---|---:|---:|---:|---|\n";
        for (const auto &p : state.planes)
            md << "| " << markdown(p.name) << " | " << p.x << " | " << p.y << " | " << p.scroll << " | " << (p.bound ? "Known to editor" : "Unresolved") << " |\n";
        md << "\n## Authoring findings\n\n";
        if (issues.empty()) md << "No current static authoring findings. This is not runtime verification.\n";
        for (const auto &i : issues) md << "- " << (i.error ? "ERROR: " : "Review: ") << markdown(i.message) << "\n";
        md << "\nGenerated by [bddtool](" << project_url << ").\n";
        out.markdown = md.str(); out.page = (folder / (name + ".md")).u8string();
        std::ofstream page(fs::u8path(out.page), std::ios::binary); page << out.markdown; page.close();
        require(bool(page), "Could not write the wiki page.");
        out.zip = (folder / (name + "-stage.zip")).u8string();
        ZipWriter *zip = zip_writer_open(out.zip.c_str()); require(zip != nullptr, "Cannot create stage ZIP.");
        bool ok = true;
        for (const auto &ext : {".BDB", ".BDD", ".bddstudio", ".BDD.meta", ".md"}) {
            auto file = name + ext; ok = zip_writer_add_file(zip, (folder / file).u8string().c_str(), file.c_str()) && ok;
        }
        for (const auto &file : {"arena_layout.png", "arena_game.png"})
            ok = zip_writer_add_file(zip, (folder / file).u8string().c_str(), file) && ok;
        for (const auto &im : state.assets->data.images) {
            auto file = "props/" + std::to_string(im.idx) + ".png";
            ok = zip_writer_add_file(zip, (folder / file).u8string().c_str(), file.c_str()) && ok;
        }
        bool closed = zip_writer_close(zip); require(closed && ok, "ZIP failed; this folder is incomplete. Build a new bundle before submitting.");
        std::string body = "## Stage\n\n" + name + "\n\nAttach `" + name + "-stage.zip` here. The generated page inside contains credits, testing notes, previews and authoring findings.\n\n"
            "- [ ] ZIP attached\n- [ ] I reviewed all included artwork and have permission to redistribute it\n- [ ] Testing status is accurately described in the page\n";
        out.submission_url = std::string(project_url) + "/issues/new?labels=stage-submission&title=" + url_encode("[stage] " + name) + "&body=" + url_encode(body);
        out.ready = true;
    } catch (const std::exception &e) { out.error = e.what(); }
    return out;
}
} // namespace studio
