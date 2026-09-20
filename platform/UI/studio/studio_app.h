#pragma once
#include "Core/studio_document.h"
namespace studio {
int run(int argc, char **argv);
void read_runtime_defaults(Document &document);
int game_export_smoke(const std::string &output, const std::string &input,
                      const std::string &game_root);
} // namespace studio
