#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

bool prompt_open_image_file(fs::path& out_path);
bool prompt_open_image_files(std::vector<fs::path>& out_paths);
bool prompt_select_image_directory(fs::path& out_path);
bool prompt_select_output_directory(fs::path& out_path);
