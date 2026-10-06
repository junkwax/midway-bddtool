#include "Core/studio_composite.h"
#include <iostream>
#include <stdexcept>

using namespace studio;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() {
    try {
        auto document = Document::demo();
        const auto &state = document.state();
        auto stage = render_composite(state, {});
        require(stage.ready && stage.width > 0 && stage.height > 0, "Stage composite failed");
        require(stage.metadata.find("bddtool-composite") != std::string::npos, "Metadata format missing");
        require(stage.metadata.find("\"scope\": \"stage\"") != std::string::npos, "Stage scope missing");
        CompositeRequest one; one.scope = CompositeScope::Selection; one.selection = {state.objects.front().id};
        auto selection = render_composite(state, one);
        require(selection.ready, "Selection composite failed");
        auto item = state.objects.front(); size_t slot = 0;
        auto *image = document.image(item.object.ii, &slot);
        require(image && selection.width == image->w && selection.height == image->h, "Selection was not tightly cropped");
        require(selection.metadata.find(std::to_string(item.id)) != std::string::npos, "Selected object missing from metadata");
        CompositeRequest layer; layer.scope = CompositeScope::Layer; layer.plane = item.plane;
        auto plane = render_composite(state, layer);
        require(plane.ready && plane.metadata.find("\"scope\": \"layer\"") != std::string::npos, "Layer export failed");
        one.selection = {999999};
        require(!render_composite(state, one).ready, "Missing selection was accepted");
        State hidden = state; hidden.objects.front().hidden = true; one.selection = {item.id};
        require(!render_composite(hidden, one).ready, "Hidden selection was exported");
        CompositeRequest backdrop; backdrop.include_backdrop = true;
        hidden = state; hidden.backdrop = 0x7c00;
        auto opaque = render_composite(hidden, backdrop);
        require(opaque.ready && opaque.rgba[3] == 255, "Backdrop was not included");
        std::cout << "Resolved stage, layer, selection, visibility, metadata and backdrop composites passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
