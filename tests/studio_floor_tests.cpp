#include "Core/studio_floor.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
void write(const fs::path &p, const std::string &bytes) { std::ofstream(p, std::ios::binary) << bytes; }
uint32_t sample(const Document &doc, int x, int y) {
    uint32_t result = 0;
    for (const auto &p : doc.scene()) {
        int sx = x - (int)p.rect.x, sy = y - (int)p.rect.y;
        const auto &im = doc.state().assets->data.images[p.image_slot];
        if (sx < 0 || sy < 0 || sx >= im.w || sy >= im.h) continue;
        auto index = im.pix[sy * im.w + sx];
        if (index) result = doc.state().assets->data.palettes[p.palette].argb[index];
    }
    return result;
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Scratch directory required.");
        auto root = fs::u8path(argv[1]) / std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        fs::create_directories(root / "src"); fs::create_directories(root / "data");
        std::string assembly = "test_mod\n\t.long calla_test\n\t.long scroll\n\t.long dlists\n\t.long bak1mods\n"
            "\t.long PLANE1BMOD\n\t.long PLANE2BMOD\n\t.long PLANE3BMOD\n\t.long 0ffffffffh\n"
            "calla_test\n\tmovi test_floor_info,a0\n\tcallr setup_floor_info\n"
            "test_floor_info\n\t.long FL_TEST\n\t.long TEST_P\n\t.word 0d7h\n\t.word 2\n\t.long scrollx\n\t.long skew\n"
            "dlists\n\t.long baklst1,worldtlx+16\n\t.long -1,floor_code\n\t.long baklst2,worldtlx2+16\n\t.long baklst3,worldtlx3+16\n\t.long 0\n"
            "TEST_P:\n\t.word 3\n\t.word 0,07c00h,003e0h\n";
        write(root / "src/BGND.ASM", assembly);
        std::string raw(1800, 0);
        for (int i = 0; i < 2400; ++i) {
            int bit = i * 6, value = i % 3;
            raw[bit / 8] = char((unsigned char)raw[bit / 8] | (value << (bit % 8)));
            if (bit % 8 > 2) raw[bit / 8 + 1] = char((unsigned char)raw[bit / 8 + 1] | (value >> (8 - bit % 8)));
        }
        write(root / "data/FL_TEST.BIN", raw);
        auto demo = Document::demo(); auto before = demo.state().assets; auto rev = demo.state().revision;
        auto floor = load_floor_preview(demo, root.u8string()); require(floor.ready(), floor.notice);
        auto &bank = *floor.layout.artwork.assets;
        require(bank.data.images[0].w == 1200 && bank.data.images[0].h == 2, "Floor dimensions");
        for (int i = 0; i < 2400; ++i) require(bank.data.images[0].pix[i] == i % 3 + 1, "6-bit decode or opaque zero lost");
        require(bank.data.palettes[0].argb[1] == 0xff000000 && floor.layout.backgrounds_before == 1, "Palette zero or floor order");
        auto r = floor.rect(demo.state(), {double(demo.state().start_x), double(demo.state().start_y)});
        require(r.x == -400 && r.y == 215 && demo.state().assets == before && demo.state().revision == rev, "Floor preview changed document or projection");
        write(root / "data/FL_TEST.BIN", raw.substr(1));
        require(!load_floor_preview(demo, root.u8string()).ready(), "Truncated floor accepted");
        write(root / "data/FL_TEST.BIN", raw);
        write(root / "src/BGND.ASM", assembly + assembly);
        require(!load_floor_preview(demo, root.u8string()).ready(), "Ambiguous floor accepted");
        write(root / "src/BGND.ASM", assembly);
        require(!load_floor_preview(Document::empty(), root.u8string()).ready(), "Unrelated map matched floor");

        AssetBank art = bank;
        auto &im = art.data.images[0]; im.w = 251; im.h = 2; im.pix.resize(502);
        for (size_t i = 0; i < im.pix.size(); ++i) im.pix[i] = i % 4; // Includes authored transparency.
        auto doc = Document::empty(); auto original = doc.state().assets;
        std::string error; int plane = -1;
        require(doc.add_floor(art, -7, 201, 1007, error, plane), error);
        require(plane == 1 && doc.state().planes[plane].name == "Floor" && !doc.state().planes[plane].bound, "New floor layer binding");
        require(doc.state().assets->data.images.size() == 3 && doc.state().objects.size() == 9, "Repeats did not reuse blocks");
        for (const auto &tile : doc.state().assets->data.images) require(tile.w <= 248 && tile.w % 4 == 0, "Invalid block width");
        auto verify = [&](const Document &d) {
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 1007; ++x) {
                auto index = im.pix[y * im.w + x % im.w];
                require(sample(d, x - 7, 201 + y) == (index ? art.data.palettes[0].argb[index] : 0), "Floor pixels or repeat seam changed");
            }
            require(sample(d, 1000, 201) == 0, "Final padding paints outside floor width");
        };
        verify(doc);
        require(doc.undo() && doc.state().assets == original && doc.state().planes.size() == 1 && doc.state().objects.empty(), "Floor is not one undo action");
        require(doc.redo(), "Redo floor"); verify(doc);
        auto path = (root / "floor.BDB").u8string(); require(doc.save(path, error), error);
        Document reopen; require(reopen.load(path, error), error); verify(reopen);
        auto saved = doc.state().assets; rev = doc.state().revision;
        require(!doc.add_floor(art, 0, 0, 0, error, plane) && doc.state().assets == saved && doc.state().revision == rev, "Failed floor changed document");
        art.data.images[0].pix[0] = 255;
        require(!doc.add_floor(art, 0, 0, 400, error, plane) && doc.state().assets == saved, "Invalid palette indices accepted");
        if (argc >= 4) {
            Document real; require(real.load(argv[2], error), error);
            auto preview = load_floor_preview(real, argv[3]); require(preview.ready(), preview.notice);
            std::cout << "Local floor decoded: " << preview.label << "\n";
        }
        std::cout << "Floor reference, raw pixels, palette, failure isolation, repeat seams, undo/redo and save/reopen passed.\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
