#include "Core/studio_art_audit.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;
void require(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
void write(const fs::path &path, const std::string &text) { std::ofstream out(path); out << text; }
const ArtAuditEntry &entry(const ArtAudit &audit, int id, bool palette = false) {
    for (const auto &e : audit.entries) if (e.id == id && e.palette == palette) return e;
    throw std::runtime_error("Missing audit row");
}
bool contains(const ArtAuditEntry &e, const std::string &text) {
    for (const auto &line : e.evidence) if (line.find(text) != std::string::npos) return true;
    return false;
}
int main(int argc, char **argv) {
    try {
        require(argc >= 2, "Need scratch directory");
        auto root = fs::absolute(fs::u8path(argv[1]) /
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "src");
        fs::create_directories(root / "data");
        fs::create_directories(root / "tmp" / "art_ref_graph");
        BddCorePalette pals[3]{};
        for (int p = 0; p < 3; p++) {
            pals[p].count = 4;
            std::snprintf(pals[p].name, sizeof pals[p].name, "PAL%d", p);
            for (int i = 0; i < 4; i++) {
                pals[p].rgb555[i] = (uint16_t)i;
                pals[p].argb[i] = bdd_core_rgb555_to_argb(i);
            }
        }
        BddCoreImage images[5];
        const char *names[] = {"PLACED", "RUNTIME", "SOURCED", "DEAD_ART", "COMMENT_ONLY"};
        BddImageMetadata meta[5]{};
        for (int i = 0; i < 5; i++) {
            images[i].idx = i; images[i].w = 16; images[i].h = 8;
            images[i].pix.assign(128, (uint8_t)(i % 3 + 1));
            meta[i].idx = i;
            std::snprintf(meta[i].label, sizeof meta[i].label, "%s", names[i]);
        }
        meta[1].frm = 1;
        BddCoreSaveResult saved{};
        auto bdd = (root / "data" / "fixture.BDD").u8string();
        require(bdd_core_save_bdd(bdd.c_str(), images, 5, pals, 3, &saved) != 0, saved.error);
        require(bdd_metadata_save_records(bdd.c_str(), meta, 5) != 0, "Metadata save failed");
        write(root / "data" / "fixture.BDB", "AUDIT 400 254 255 1 3 1\nLAYER1 0 399 0 253\n4000 0 0 0 0\n");
        write(root / "src" / "TEST.ASM",
              "* COMMENT_ONLY\n .long SOURCED ; COMMENT_ONLY\n .string \"COMMENT_ONLY\"\n .long PAL2\n");
        auto graph = root / "tmp" / "art_ref_graph" / "current.json";
        write(graph, R"([{"label":"DEAD_ART","table":"FIRST","class":"DEAD","node":1,"extra":[true,null,{"label":"COMMENT_ONLY"}]},{"label":"DEAD_ART","table":"SECOND","class":"WINDOW"}])");
        Document doc; std::string error;
        require(doc.load((root / "data" / "fixture.BDB").u8string(), error), error);
        auto bank = doc.state().assets; auto rev = doc.state().revision;
        auto audit = audit_art(doc, root.u8string());
        require(audit.error.empty(), audit.error);
        require(audit.unplaced_images == 4 && audit.unplaced_palettes == 2, "Incorrect unplaced counts");
        require(entry(audit, 0).status == "Placed", "Placed image not protected");
        require(entry(audit, 1).status == "Runtime metadata", "Animation metadata ignored");
        require(entry(audit, 2).status == "Source mention" && contains(entry(audit, 2), "TEST.ASM:2"), "Source evidence missing");
        require(entry(audit, 3).status == "Graph evidence" && contains(entry(audit, 3), "ambiguous label") &&
                contains(entry(audit, 3), "WINDOW") && contains(entry(audit, 3), "DEAD"), "Ambiguous graph evidence lost");
        require(entry(audit, 4).status == "Unresolved", "Comment, string or nested JSON produced a false reference");
        require(entry(audit, 2, true).status == "Source mention" && entry(audit, 2, true).duplicate_of == 0,
                "Palette usage/duplicate evidence missing");
        require(doc.state().assets == bank && doc.state().revision == rev, "Audit mutated document");
        // Image 3 shares the placed payload; images 1 and 4 share another. Count each payload once.
        auto expected = (optimization_image_bits(images[1]) + optimization_image_bits(images[2])) / 8;
        require(audit.unplaced_video_bytes == expected, "Unplaced estimate double-counted shared pixels");
        require(art_audit_report(audit).find("DEAD is a review candidate") != std::string::npos,
                "Graph freshness limitation missing from report");
        auto local = audit_art(doc, "");
        require(local.error.empty() && entry(local, 3).status == "Unresolved", "No-root audit claimed external proof");
        write(graph, R"([{"label":"DEAD_ART","label":"RUNTIME","table":"FIRST","class":"DEAD"}])");
        auto malformed = audit_art(doc, root.u8string());
        require(!malformed.error.empty() && entry(malformed, 3).status == "Unresolved", "Malformed graph trusted");
        write(graph, R"([{"label":"DEAD_ART","table":"FIRST","class":"DEAD","unknown":[1,]}])");
        require(!audit_art(doc, root.u8string()).error.empty(), "Malformed unknown JSON field accepted");
        write(graph, R"([{"label":"DEAD\u005fART","table":"FIRST","class":"AUTHORED"}])");
        auto escaped = audit_art(doc, root.u8string());
        require(escaped.error.empty() && contains(entry(escaped, 3), "AUTHORED"), "Escaped JSON label did not match");
        OptimizeProgress progress; progress.cancel = true;
        require(audit_art(doc, root.u8string(), &progress).cancelled, "Cancellation ignored");
        if (argc >= 4) {
            Document real;
            require(real.load(argv[2], error), error);
            auto actual = audit_art(real, argv[3]);
            require(actual.error.empty(), actual.error);
            write(root / "real-art-audit.txt", art_audit_report(actual));
            std::cout << "Real audit: " << actual.source_files << " files, " << actual.unplaced_images
                      << " unplaced images, " << actual.unplaced_palettes << " palettes without placements.\n";
        }
        std::cout << "Unused-art audit evidence, graph parsing, ambiguity, accounting and read-only checks passed.\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
