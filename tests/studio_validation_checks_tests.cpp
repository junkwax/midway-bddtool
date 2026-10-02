#include "../tools/studio_validation_checks.h"
#include <iostream>
using namespace studio;
using namespace studio::validation;

BddCoreImage image(int id, int w, int h, uint8_t pixel) {
    BddCoreImage im{};
    im.idx = id;
    im.w = w;
    im.h = h;
    im.pix.assign(size_t(w) * h, pixel);
    return im;
}
Placement placement(int image, int y, int z = 0, int flags = 0) {
    Placement p;
    p.plane = 0;
    p.object.ii = image;
    p.object.sy = y;
    p.object.wx = (z << 8) | flags;
    p.object.order = image;
    return p;
}
int main() {
    try {
        auto bank = std::make_shared<AssetBank>();
        bank->data.images = {image(0, 4, 4, 1), image(1, 4, 2, 2), image(2, 4, 3, 1),
                             image(3, 4, 1, 1)};
        BddCorePalette pal{};
        pal.count = 3;
        pal.rgb555[1] = 0; // Opaque black must not become transparent.
        pal.rgb555[2] = 31;
        bank->data.palettes.push_back(pal);
        State original;
        original.assets = bank;
        original.objects = {placement(0, 0), placement(1, 2)};
        auto split = original;
        split.objects = {placement(2, 0), placement(3, 3), placement(1, 2)};
        BddCoreModule bounds{};
        bounds.x2 = bounds.y2 = 3;
        for (bool reverse : {false, true}) {
            auto pixels = layer(original, 0, bounds, reverse);
            check(pixels[0] == 0x10000 && pixels[12] == 0x1001f, "Y order/opaque black");
            check(pixels != layer(split, 0, bounds, reverse),
                  "Subdivision changing Y priority escaped verification");
        }
        // Explicit Z priority keeps the split pieces behind their neighbor.
        original.objects[1].object.wx = 0x100;
        split.objects[2].object.wx = 0x100;
        for (bool reverse : {false, true})
            check(layer(original, 0, bounds, reverse) == layer(split, 0, bounds, reverse),
                  "Z priority was ignored");
        // Equal Z/Y objects are sensitive to arrival order.
        original.objects = {placement(0, 0), placement(1, 0)};
        check(layer(original, 0, bounds, false) != layer(original, 0, bounds, true),
              "Equal-depth tie orders were not exercised");
        // Both flip axes and transparent index zero.
        bank->data.images[0] = image(0, 2, 2, 0);
        bank->data.images[0].pix = {0, 1, 2, 0};
        original.objects = {placement(0, 0, 0, 0x30)};
        auto flipped = layer(original, 0, bounds, false);
        check(flipped[0] == 0 && flipped[1] == 0x1001f && flipped[4] == 0x10000,
              "Flips or transparency changed");
        original.objects[0].object.depth = 4;
        bool refused = false;
        try {
            layer(original, 0, bounds, false);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "Out-of-bounds artwork was silently clipped");

        std::vector<BddCoreImage> images = {image(1, 20, 49, 1), image(2, 16, 14, 1),
                                            image(3, 13, 17, 1)};
        check(protects_unaligned(images, 1) && protects_unaligned(images, 2),
              "Unaligned compression lost its prior buffer context");
        check(!protects_unaligned(images, 3), "Final image protects nonexistent successors");
        images.insert(images.begin() + 1, image(4, 32, 32, 1));
        check(!protects_unaligned(images, 1) && protects_unaligned(images, 4),
              "A complete buffer overwrite did not delimit context");
        images.back() = image(3, 16, 17, 1);
        check(!protects_unaligned(images, 4), "Aligned artwork incorrectly protects context");
        std::cout << "Runtime validation checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
