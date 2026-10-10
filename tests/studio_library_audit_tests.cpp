#include "Core/studio_library_audit.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace studio;
namespace fs = std::filesystem;
void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        auto root = fs::u8path(argv[1]); fs::remove_all(root); fs::create_directories(root);
        std::ofstream(root / "summary.csv")
            << "file,scope,baseline_bytes,preserve_saved,best_saved,best_mode,extra_objects,extra_table_bytes,extra_palette_bytes,palettes_after,unverified_modes,detail\n"
            << "data/SMALL.BDD,not in active BBB list,2000,100,300,palette,2,24,16,8,,reports/001/\n"
            << "data/BIG.BDD,\"active, primary\",9000,500,1200,preserve,4,48,0,7,\"palette; shared\",reports/000/\n";
        LibraryAuditReport report; std::string error;
        require(load_library_audit(root.u8string(), report, error), error);
        require(report.rows.size() == 2 && report.rows[0].file == "data/BIG.BDD", "Results were not sorted by saving");
        require(report.rows[0].scope == "active, primary" && report.rows[0].unverified_modes == "palette; shared", "Quoted CSV changed");
        require(report.rows[0].active && !report.rows[1].active, "Active build relationships were classified incorrectly");
        require(report.preserve_total == 500 && report.best_total == 1200 && report.incomplete == 1, "Active report totals are wrong");
        fs::create_directories(root / "bad"); std::ofstream(root / "bad" / "summary.csv") << "file,scope\nA,B\n";
        require(!load_library_audit((root / "bad").u8string(), report, error) && error.find("missing column") != std::string::npos,
                "Malformed report was accepted");
        fs::create_directories(root / "unsafe");
        std::ofstream(root / "unsafe" / "summary.csv")
            << "file,scope,baseline_bytes,preserve_saved,best_saved,best_mode,extra_objects,extra_table_bytes,extra_palette_bytes,palettes_after,unverified_modes,detail\n"
            << "../../LIVE.BDD,active BBB input,1,1,1,preserve,0,0,0,1,,../../\n";
        require(!load_library_audit((root / "unsafe").u8string(), report, error) && error.find("unsafe path") != std::string::npos,
                "Report path traversal was accepted");
        LibraryAuditReport copy_report; copy_report.folder = root.u8string();
        LibraryAuditRow copy_row; copy_row.file = "data/STAGE.BDD";
        fs::create_directories(root / "inputs/data");
        std::ofstream(root / "inputs/data/STAGE.BDD") << "bdd";
        std::ofstream(root / "inputs/data/STAGE.BDB") << "bdb";
        std::ofstream(root / "inputs/data/STAGE.BDD.meta") << "meta";
        std::ofstream(root / "inputs/data/OTHER.BDD") << "other";
        std::string copied_path;
        require(prepare_library_audit_review(copy_report, copy_row, (root / "review").u8string(), copied_path, error), error);
        require(fs::exists(copied_path) && fs::exists(root / "review/STAGE.BDB") && fs::exists(root / "review/STAGE.BDD.meta") &&
                !fs::exists(root / "review/OTHER.BDD"), "Scratch review companions are wrong");
        require(!prepare_library_audit_review(copy_report, copy_row, (root / "review").u8string(), copied_path, error),
                "Existing scratch review folder was overwritten");
        OptimizeOptions options; int engine = -1;
        copy_row.best_mode = "palette_shared";
        require(library_audit_optimize_settings(copy_row, options, engine, error), error);
        require(options.deep && options.compact_palettes && options.max_added_objects == 24 &&
                options.source_image == -1 && engine == 1, "Shared palette audit settings changed");
        copy_row.best_mode = "preserve";
        require(library_audit_optimize_settings(copy_row, options, engine, error) &&
                options.deep && !options.compact_palettes && engine == 0,
                "Preserve audit settings changed");
        copy_row.best_mode = "unknown";
        require(!library_audit_optimize_settings(copy_row, options, engine, error),
                "Unknown audit mode was accepted");
        fs::remove_all(root);
        std::cout << "Library audit CSV validation, sorting and totals passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
