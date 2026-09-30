#include "Core/studio_optimizer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace studio;
namespace fs = std::filesystem;

// Analysis only: this utility never applies a proposal or saves a document.
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: studio_savings_audit source.BDD new-report-directory");
        auto folder = fs::absolute(fs::u8path(argv[2]));
        if (fs::exists(folder))
            throw std::runtime_error("Report directory already exists");
        fs::create_directories(folder);
        Document doc;
        std::string error;
        if (!doc.load(argv[1], error))
            throw std::runtime_error(error);
        // Retain source coordinates; no runtime imports or game scripts are executed.
        for (size_t i = 0; i < doc.state().planes.size(); ++i)
            if (doc.state().planes[i].name == "mk3cave3")
                doc.set_plane_flags((int)i, false, true);
        auto base = optimization_budget(doc.state());
        uint64_t payload_sum = 0, unplaced = 0;
        int unused = 0, unassigned = 0;
        std::set<int> placed;
        for (const auto &p : doc.state().objects) {
            placed.insert(p.object.ii);
            if (p.plane < 0)
                ++unassigned;
        }
        for (const auto &im : doc.state().assets->data.images) {
            payload_sum += optimization_image_bits(im) / 8;
            if (!placed.count(im.idx)) {
                ++unused;
                unplaced += optimization_image_bits(im) / 8;
            }
        }
        std::ofstream summary(folder / "summary.csv");
        summary << "mode,verified,baseline_bytes,proposed_bytes,saved_bytes,table_delta,palette_"
                   "delta,objects_before,objects_after,palettes_before,palettes_after,changed_"
                   "images,skipped_notes\n";
        std::ofstream inventory(folder / "inventory.txt");
        inventory << "images=" << base.images << "\nobjects=" << base.objects
                  << "\npalettes=" << base.palettes << "\nbaseline_bytes=" << base.video_bits / 8
                  << "\nundeduplicated_bytes=" << payload_sum << "\nunplaced_images=" << unused
                  << "\nunplaced_bytes=" << unplaced << "\nunassigned_placements=" << unassigned
                  << "\npaired=" << doc.state().has_bdb << "\nnotice=" << doc.notice() << '\n';
        for (int mode = 0; mode < 4; ++mode) {
            OptimizeOptions options;
            options.deep = true;
            options.max_added_objects = 24;
            options.compact_palettes = mode >= 2;
            const std::string name = mode == 0   ? "preserve"
                                     : mode == 1 ? "preserve_shared"
                                     : mode == 2 ? "palette"
                                                 : "palette_shared";
            std::cout << name << std::endl;
            auto plan =
                mode % 2 ? find_shared_savings(doc, options) : find_lossless_savings(doc, options);
            std::ofstream detail(folder / (name + ".txt"));
            detail << optimization_report(plan) << "\nError: " << plan.error << '\n';
            auto after = plan.verified ? plan.proposed : base;
            summary << name << ',' << plan.verified << ',' << base.video_bits / 8 << ','
                    << after.video_bits / 8 << ','
                    << (int64_t(base.video_bits) - int64_t(after.video_bits)) / 8 << ','
                    << int64_t(after.table_bytes) - int64_t(base.table_bytes) << ','
                    << int64_t(after.palette_bytes) - int64_t(base.palette_bytes) << ','
                    << base.objects << ',' << after.objects << ',' << base.palettes << ','
                    << after.palettes << ',' << plan.changes.size() << ',' << plan.notes.size()
                    << '\n';
            summary.flush();
        }
        if (!summary || !inventory)
            throw std::runtime_error("Could not write audit report");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
