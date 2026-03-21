#pragma once

#include "app_state.h"

bool is_supported_image(const fs::path& file_path);
fs::path parse_user_path_input(const char* raw_input);
const char* output_format_label(int format);
std::string display_name(const fs::path& path);
void load_path(AppState& state, const fs::path& input_path);
void load_paths(AppState& state, const std::vector<fs::path>& input_paths);
void load_selected_image(AppState& state, int index);
void clear_current_image(AppState& state);
bool resize_selected_and_save(AppState& state);
void resize_all_and_save(AppState& state);
