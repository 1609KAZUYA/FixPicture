#pragma once

#include <GLFW/glfw3.h>

#include <filesystem>
#include <string>
#include <vector>

#include "image_core.h"

namespace fs = std::filesystem;

struct GLTexture {
  GLuint id = 0;
  int width = 0;
  int height = 0;

  void destroy() {
    if (id != 0) {
      glDeleteTextures(1, &id);
      id = 0;
    }
    width = 0;
    height = 0;
  }
};

struct AppState {
  char path_input[1024] = "";
  char output_input[1024] = "";
  fs::path current_dir;
  std::vector<fs::path> files;
  int selected_index = -1;
  int library_page = 0;

  Image current_image = {0, 0, 0, nullptr};
  GLTexture preview_texture;

  int new_width = 0;
  int new_height = 0;
  int output_format = IMG_FORMAT_PNG;
  int jpg_quality = 90;
  bool last_save_succeeded = false;
  std::string status = "No file or directory loaded yet.";
};
