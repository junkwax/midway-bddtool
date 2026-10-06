#include "Core/studio_floor.h"
#include "Core/studio_mk3_layout.h"
#include "Core/img_format.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
void write(const fs::path &p, const std::string &bytes) { std::ofstream(p, std::ios::binary) << bytes; }
template<class T> void append(std::string &s, const T &v) { s.append((const char *)&v, sizeof v); }
std::string floor_img() {
    ImgLibHeaderDisk h{}; h.imgcnt = 4; h.palcnt = 5; h.version = 0x500; h.temp = 0xabcd; h.oset = sizeof h;
    ImgImageDisk strips[4]{}; ImgPaletteDisk palettes[2]{};
    size_t offset = sizeof h + sizeof strips + sizeof palettes;
    for (int i = 0; i < 2; ++i) { palettes[i].numc = 2; palettes[i].oset = (unsigned)offset; offset += 4; }
    for (int i = 0; i < 4; ++i) {
        auto &s = strips[i]; std::snprintf(s.name, sizeof s.name, "%s%d", i < 2 ? "A" : "B", i % 2 + 1);
        s.w = 4; s.h = i == 1 ? 1 : 2; s.palnum = i == 1 ? 4 : 3;
        s.oset = (unsigned)offset; s.flags = i == 1 ? 0x80 : 0;
        offset += i == 1 ? 3 : 8;
    }
    std::string bytes; append(bytes, h); append(bytes, strips); append(bytes, palettes);
    bytes.append("\0\0\0\x7c\x1f\0\xe0\3", 8); // black/red and blue/green
    bytes.append("\0\1\1\0\1\1\1\1", 8);
    bytes.append("\x11\0\1", 3); // transparent margins with a stored, opaque zero
    bytes.append(16, char(1));
    return bytes;
}
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

        std::string mk3 = "test_mod\n\t.word 0,7,240,400,0,1200-scrrgt\n"
            "\t.long calla\n\t.long rates\n\t.long lists\n\t.long bak1mods\n"
            "\t.long PLANE1BMOD\n\t.word 0,0\n\t.long PLANE2BMOD\n\t.word 0,20\n\t.long PLANE3BMOD\n\t.word 0,30\n"
            "\t.long center_x\n\t.long PLANE1BMOD,worldtlx1\n\t.long 0\n"
            "\t.long FL_TEST\n\t.long TEST_P\n\t.word 2\n\t.long scrollx1\n\t.long skew_7\n"
            "rates\n\t.long 0,0,0,0,0,>8000,>20000,>10000,>20000\n"
            "lists\n\t.long baklst1,worldtlx1+16\n\t.long -1,shadow_code\n\t.long -1,floor_code\n"
            "\t.long baklst2,worldtlx2+16\n\t.long baklst3,worldtlx3+16\n\t.long 0\n";
        const std::string palette = "TEST_P:\n\t.word 3\n\t.word 0,07c00h,003e0h\n";
        const auto mkbt = root / "src/MKBT.ASM";
        fs::create_directories(root / "BINFILES"); write(root / "BINFILES/FL_TEST.BIN", raw);
        write(mkbt, mk3 + palette);
        auto mk3doc = Document::demo(); auto mk3before = mk3doc.state();
        auto load_mk3 = [&] { return load_mk3_floor_preview(mk3doc, mkbt.u8string(), root.u8string()); };
        auto reference = load_mk3(); require(reference.ready(), reference.notice);
        require(reference.mk3 && reference.screen_y == 252 && reference.scroll == .5 &&
                reference.layout.backgrounds_before == 1, "MK3 floor alignment, scroll or draw order");
        require(fs::u8path(reference.source) == root / "BINFILES/FL_TEST.BIN" && fs::u8path(reference.palette_source) == mkbt, "MK3 floor source provenance");
        require(reference.layout.artwork.assets->data.images[0].pix == bank.data.images[0].pix &&
                reference.layout.artwork.assets->data.palettes[0].argb[1] == 0xff000000, "MK3 raw floor pixels or opaque zero");
        require(mk3doc.state().revision == mk3before.revision && mk3doc.state().assets == mk3before.assets, "MK3 floor read changed document");
        std::string mk3error;
        require(mk3doc.apply_mk3_layout(read_mk3_layout(mk3doc, mkbt.u8string()), mk3error), mk3error);
        auto start = reference.rect(mk3doc.state(), {400,7}), moved = reference.rect(mk3doc.state(), {480,10});
        require(start.x == -400 && start.y == 252 && moved.x == -440 && moved.y == 249, "MK3 floor camera projection");
        auto labeled = mk3; labeled.insert(labeled.find("\t.long FL_TEST"), "test_floor_info\n");
        write(mkbt, labeled); write(root / "src/BGNDPAL.ASM", palette);
        reference = load_mk3(); require(reference.ready(), reference.notice);
        require(fs::u8path(reference.palette_source) == root / "src/BGNDPAL.ASM", "External floor palette not used");
        write(root / "src/BGNDPAL.ASM", "UNRELATED:\n\t.word 1,0\n");
        require(!load_mk3().ready(), "Missing MK3 palette accepted");
        write(mkbt, mk3 + palette);
        write(root / "BINFILES/FL_TEST.BIN", raw.substr(1));
        require(!load_mk3().ready(), "Truncated MK3 texture accepted");
        auto invalid_pixels = raw; invalid_pixels[0] = 63;
        write(root / "BINFILES/FL_TEST.BIN", invalid_pixels);
        require(!load_mk3().ready(), "Absent MK3 palette index accepted");
        write(root / "BINFILES/FL_TEST.BIN", raw);
        auto missing = mk3; missing.replace(missing.find("FL_TEST"), 7, "FL_MISSING"); write(mkbt, missing + palette);
        require(!load_mk3().ready(), "Missing MK3 texture accepted");
        auto no_floor = mk3; no_floor.replace(no_floor.find("FL_TEST"), 7, "0"); write(mkbt, no_floor + palette);
        require(!load_mk3().ready(), "Stage without MK3 floor accepted");
        auto duplicate = mk3; duplicate.insert(duplicate.find("\t.long -1,floor_code"), "\t.long -1,floor_code\n");
        write(mkbt, duplicate + palette); require(!load_mk3().ready(), "Duplicate MK3 floor callback accepted");
        write(mkbt, mk3 + palette);
        require(!load_mk3_floor_preview(Document::empty(), mkbt.u8string(), root.u8string()).ready(), "Unrelated MK3 map matched floor");

        auto imgpath = root / "floors.IMG"; auto imgbytes = floor_img(); write(imgpath, imgbytes);
        auto library = inspect_floor_library(imgpath.u8string());
        require(library.error.empty() && library.entries.size() == 2 && library.entries[0].strips == std::vector<std::string>({"A1", "A2"}), "Floor groups or strip order");
        auto alternative = load_floor_library_entry(library, 0, reference);
        require(alternative.ready(), alternative.notice);
        require(alternative.scroll == reference.scroll && alternative.screen_y == 252 && alternative.screen_x == 196 &&
                alternative.layout.backgrounds_before == reference.layout.backgrounds_before, "Alternative floor lost runtime alignment");
        const auto &ab = *alternative.layout.artwork.assets;
        auto pixel = [&](int x, int y) { return ab.data.palettes[0].argb[ab.data.images[0].pix[y * 8 + x]]; };
        require(pixel(0,0) == 0xff000000 && pixel(1,0) == 0xffff0000 && pixel(5,1) == 0xff0000ff && pixel(6,1) == 0xff00ff00,
                "IMG floor zero, strip seams or mixed palettes changed");
        require(pixel(4,0) == 0 && pixel(4,1) == 0 && pixel(7,1) == 0, "Shorter strip padding or trimmed margins became opaque");
        auto other = load_floor_library_entry(library, 1, {});
        require(other.ready() && other.label == "B" && other.screen_y == 252, "Unused alternative or no-runtime fallback");
        require(!load_floor_library_entry(library, 99, {}).ready(), "Out-of-range floor accepted");
        auto remapped = std::make_shared<AssetBank>(ab);
        std::swap(remapped->data.palettes[0].argb[1], remapped->data.palettes[0].argb[2]);
        std::swap(remapped->data.palettes[0].rgb555[1], remapped->data.palettes[0].rgb555[2]);
        for (auto &p : remapped->data.images[0].pix) { if (p == 1) p = 2; else if (p == 2) p = 1; }
        auto reordered = alternative; reordered.label = "OTHER_NAME"; reordered.layout.artwork.assets = remapped;
        auto comparison = compare_floor_pixels(reordered, alternative);
        require(comparison.match == FloorMatch::Exact && comparison.total_pixels == 16 && comparison.different_pixels == 0,
                "Floor comparison depends on palette indices or label");
        auto changed = std::make_shared<AssetBank>(*remapped); changed->data.images[0].pix[0] = 0;
        auto changed_floor = reordered; changed_floor.layout.artwork.assets = changed;
        comparison = compare_floor_pixels(changed_floor, alternative);
        require(comparison.match == FloorMatch::DifferentPixels && comparison.different_pixels == 1,
                "Opaque zero and transparency compare equal");
        require(compare_floor_pixels(alternative, reference).match == FloorMatch::DifferentSize, "Different floor size not reported");
        require(compare_floor_pixels(alternative, {}).match == FloorMatch::Unavailable, "Missing runtime floor reported as a difference");
        changed->data.images[0].pix[0] = 255;
        require(compare_floor_pixels(changed_floor, alternative).match == FloorMatch::Unavailable, "Invalid floor pixels compared");

        auto nearby = root / "nearby"; fs::create_directories(nearby / "nested");
        write(nearby / "MKFLOORS.IMG", imgbytes); write(nearby / "mkfloor2.img", imgbytes);
        write(nearby / "MKFLOORS.IMG.backup", imgbytes); write(nearby / "OTHER.IMG", imgbytes);
        write(nearby / "nested/MKFLOOR3.IMG", imgbytes);
        auto found = discover_floor_libraries({nearby.u8string(), (nearby / ".").u8string(), (root / "absent").u8string()}, (nearby / "OTHER.IMG").u8string());
        require(found.notice.empty() && found.paths.size() == 3 && fs::u8path(found.paths[0]).filename() == "MKFLOORS.IMG" &&
                fs::u8path(found.paths[1]).filename() == "mkfloor2.img" && fs::u8path(found.paths[2]).filename() == "OTHER.IMG",
                "Nearby library discovery order, case, deduplication or scope");
        auto same_name = discover_floor_libraries({nearby.u8string(), (nearby / "nested").u8string()});
        require(same_name.paths.size() == 3, "Explicitly selected second folder was omitted");
        require(discover_floor_libraries({}).paths.empty(), "Empty discovery searched an implicit folder");
        auto missing_strip = imgbytes; missing_strip[sizeof(ImgLibHeaderDisk) + sizeof(ImgImageDisk) + 1] = '3';
        write(imgpath, missing_strip); auto broken = inspect_floor_library(imgpath.u8string());
        require(!broken.entries[0].problem.empty() && !load_floor_library_entry(broken, 0, {}).ready(), "Gapped floor group accepted");
        write(imgpath, imgbytes.substr(0, imgbytes.size() - 3));
        require(!load_floor_library_entry(library, 1, {}).ready(), "Truncated IMG floor accepted");
        write(imgpath, imgbytes);
        auto authored_img = Document::empty(); std::string img_error; int img_plane = -1;
        require(authored_img.add_floor(ab, 0, 252, 8, img_error, img_plane), img_error);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 8; ++x)
            require(sample(authored_img, x, y + 252) == pixel(x,y), "IMG floor copy changed composited pixels");
        require(authored_img.undo() && authored_img.state().objects.empty() && authored_img.redo(), "IMG floor copy undo/redo");

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
        if (argc >= 4 && std::string(argv[2]) == "--library") {
            auto actual = inspect_floor_library(argv[3]); require(actual.error.empty(), actual.error);
            FloorPreview actual_runtime;
            if (argc >= 6) {
                Document actual_doc; require(actual_doc.load(argv[4], error), error);
                actual_runtime = fs::u8path(argv[5]).filename() == "MKBT.ASM" ? load_mk3_floor_preview(actual_doc, argv[5]) : load_floor_preview(actual_doc, argv[5]);
                require(actual_runtime.ready(), actual_runtime.notice);
            }
            int decoded = 0, unavailable = 0;
            for (size_t i = 0; i < actual.entries.size(); ++i) {
                if (!actual.entries[i].problem.empty()) { ++unavailable; continue; }
                auto preview = load_floor_library_entry(actual, i, {}); require(preview.ready(), actual.entries[i].label + ": " + preview.notice);
                if (actual_runtime.ready()) {
                    auto match = compare_floor_pixels(preview, actual_runtime);
                    require(match.match != FloorMatch::Unavailable, "Local comparison failed");
                    std::cout << actual.entries[i].label << ": " << (match.match == FloorMatch::Exact ? "exact match" : match.match == FloorMatch::DifferentSize ? "different size" : "different pixels") << '\n';
                }
                ++decoded;
            }
            require(decoded > 0, "No floor library entries decoded");
            std::cout << "Local IMG floors: " << decoded << " decoded, " << unavailable << " unavailable. No source assets saved.\n";
        } else if (argc >= 4) {
            Document real; require(real.load(argv[2], error), error);
            auto preview = argc >= 5 && std::string(argv[4]) == "--mk3" ? load_mk3_floor_preview(real, argv[3]) : load_floor_preview(real, argv[3]);
            require(preview.ready(), preview.notice);
            std::cout << "Local floor decoded: " << preview.label << " | " << preview.source << " | " << preview.palette_source << "\n";
        }
        std::cout << "Floor reference, raw pixels, palette, failure isolation, repeat seams, undo/redo and save/reopen passed.\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
