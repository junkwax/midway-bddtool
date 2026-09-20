#include "Core/studio_animation.h"
#include "Core/studio_game_export.h"
#include "Core/img_format.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}
std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
template <class T> void append(std::string &s, const T &value) {
    s.append((const char *)&value, sizeof value);
}
std::string fixture_assembly() {
    std::string s = "forest_mod\n\t.word 0\n\t.word 224\n\t.word -8\n\t.word 200\n"
                    "\t.word 0\n\t.word 900\n\t.long calla_forest\n\t.long forest_scroll\n"
                    "\t.long forest_dlists\n\t.long bak1mods\n\t.long skip_bakmod\n";
    for (int i = 1; i <= 3; i++)
        s += "\t.long PLANE" + std::to_string(i) + "BMOD\n\t.word 0,0\n";
    s += "\t.long 0ffffffffh\nforest_scroll\n";
    for (int i = 0; i < 9; i++)
        s += "\t.long 020000h\n";
    s += "forest_dlists\n\t.long baklst2,worldtlx2+16\n\t.long baklst1,worldtlx+16\n"
         "\t.long baklst3,worldtlx3+16\n\t.long baklst4,worldtlx4+16\n\t.long 0\n"
         "calla_forest\n\tcreate pid_bani,tree_animator\n\trets\ntree_animator\n";
    for (auto value : {"00014000ah", ">0014001e", "1310770"})
        s += std::string("\tmovi ") + value + ",a4\n\tcallr make_a_mad_tree\n";
    s += "cycle\tmovi a_test,a9\n\tmovk 5,a0\n\tjsrp triple_framew\n"
         "triple_framew\n\tretp\nmake_a_mad_tree\n\tmovi baklst1,b4\n\trets\n"
         "a_test\n\t.long TREEANI1\n\t.long TREEANI2\n\t.long TREEANI1\n\t.long 0\n";
    return s;
}
std::string fixture_img() {
    ImgLibHeaderDisk h{};
    h.imgcnt = 2;
    h.palcnt = 4;
    h.temp = 0xabcd;
    h.version = 0x500;
    h.oset = sizeof h;
    ImgImageDisk a{}, b{};
    std::snprintf(a.name, sizeof a.name, "TREEANI1");
    std::snprintf(b.name, sizeof b.name, "TREEANI2");
    a.w = b.w = 4;
    a.h = b.h = 2;
    a.palnum = b.palnum = 3;
    a.anix = (unsigned short)-2;
    a.aniy = 4;
    b.anix = 3;
    b.aniy = (unsigned short)-5;
    ImgPaletteDisk p{};
    p.numc = 2;
    p.oset = sizeof h + sizeof a + sizeof b + sizeof p;
    a.oset = p.oset + 4;
    b.oset = a.oset + 8;
    b.flags = 0x80; // Second frame exercises trimmed rows and transparent margins.
    std::string bytes;
    append(bytes, h);
    append(bytes, a);
    append(bytes, b);
    append(bytes, p);
    bytes.append("\0\0\0\x7c", 4);
    bytes.append("\0\1\1\0\1\1\1\1", 8);
    bytes.append("\x11\1\1\x11\1\1", 6);
    return bytes;
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch path");
        auto root =
            fs::u8path(argv[1]) /
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        fs::create_directories(root / "src");
        fs::create_directories(root / "data");
        auto source = fixture_assembly(), img = fixture_img();
        write(root / "src" / "BGND.ASM", source);
        write(root / "data" / "MKBGANI.IMG", img);
        auto doc = Document::demo();
        auto bank = doc.state().assets;
        auto preview = load_animation_preview(doc, root.u8string());
        require(preview.ready(), preview.notice);
        require(preview.sequence == std::vector<int>({0, 1, 0}) && preview.anchors.size() == 3,
                "Source sequence or actor count changed");
        require(preview.anchors[0].x == 10 && preview.anchors[1].x == 30 &&
                    preview.anchors[2].x == 50,
                "Packed spawn coordinates decoded incorrectly");
        require(preview.frame_at(0) == 0 && preview.frame_at(.1) == 1 && preview.frame_at(.25) == 0,
                "Playback timing or wrap is incorrect");
        auto first = preview.rect(0, 0, {7, 9}), second = preview.rect(0, 1, {7, 9});
        require(first.x == 5 && first.y == 7 && second.x == 0 && second.y == 16,
                "Camera projection or per-frame signed anchors changed");
        require(preview.artwork.assets->data.images[1].pix ==
                    std::vector<uint8_t>({0, 1, 1, 0, 0, 1, 1, 0}),
                "Trimmed IMG frame did not preserve transparent margins");
        require(preview.artwork.assets->data.palettes[0].argb[1] == 0xffff0000u,
                "Source RGB555 palette was not preserved");
        require(preview.draw_rank(doc) == .5, "Actor layer slot not preserved");
        doc.reorder_plane(0, 1);
        require(preview.draw_rank(doc) == .5,
                "Actor shifted out of its display-list slot on reorder");
        AssemblyExport exported;
        std::string error;
        require(export_game_assembly(doc, source, exported, error), error);
        require(exported.text.find("\t.long baklst3,worldtlx3+16\n\t.long "
                                   "baklst1,worldtlx+16\n\t.long baklst2,worldtlx2+16") !=
                    std::string::npos,
                "Export did not preserve actor-only slot between rearranged background layers");
        auto revision = doc.state().revision;
        require(doc.save((root / "saved.BDB").u8string(), error), error);
        Document reopened;
        require(reopened.load((root / "saved.BDB").u8string(), error), error);
        require(reopened.state().assets->data.images.size() == bank->data.images.size() &&
                    doc.state().revision == revision && doc.state().assets == bank,
                "Preview animation leaked into document assets or history");
        require(read(root / "src" / "BGND.ASM") == source &&
                    read(root / "data" / "MKBGANI.IMG") == img,
                "Preview loading modified source files");
        // A later tab/load must not invalidate an already-owned preview.
        write(root / "data" / "MKBGANI.IMG", img.substr(0, 10));
        auto missing = load_animation_preview(doc, root.u8string());
        require(!missing.ready() && preview.ready() && preview.rect(0, 1, {7, 9}).y == 16,
                "Invalid source or independent preview ownership failed");
        write(root / "data" / "MKBGANI.IMG", img);
        auto unsupported = source;
        unsupported.replace(unsupported.find(".long TREEANI2"), 14, ".long ani_jump");
        write(root / "src" / "BGND.ASM", unsupported);
        require(!load_animation_preview(doc, root.u8string()).ready(),
                "Unsupported animation opcode accepted");
        if (argc >= 4) {
            Document real;
            require(real.load(argv[2], error), error);
            auto actual = load_animation_preview(real, argv[3]);
            require(actual.ready(), actual.notice);
            require(actual.frames.size() == 7 && actual.sequence.size() == 20 &&
                        actual.anchors.size() == 3,
                    "Unexpected local Forest animation shape");
            std::cout << "Local Forest: 7 images, 20 sequence steps, 3 faces loaded.\n";
        }
        std::cout << "Animation decode, timing, anchors, actor slots, isolation and "
                     "unsupported-source checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
