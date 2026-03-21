#include "image_session.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <set>
#include <vector>

#if defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#endif

std::string display_name(const fs::path& path) {
#if defined(__cpp_lib_char8_t)
  const auto u8 = path.filename().u8string();
  std::string utf8(u8.begin(), u8.end());
#else
  std::string utf8 = path.filename().u8string();
#endif

#if defined(__APPLE__)
  CFStringRef source = CFStringCreateWithCString(kCFAllocatorDefault, utf8.c_str(), kCFStringEncodingUTF8);
  if (!source) {
    return utf8;
  }

  CFMutableStringRef normalized = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, source);
  CFRelease(source);
  if (!normalized) {
    return utf8;
  }

  CFStringNormalize(normalized, kCFStringNormalizationFormC);

  const CFIndex length = CFStringGetLength(normalized);
  const CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string result(static_cast<size_t>(max_size), '\0');
  if (CFStringGetCString(normalized, result.data(), max_size, kCFStringEncodingUTF8)) {
    result.resize(std::strlen(result.c_str()));
    CFRelease(normalized);
    return result;
  }

  CFRelease(normalized);
#endif

  return utf8;
}

static void set_path_input(AppState& state, const fs::path& path) {
  const std::string text = path.string();
  std::snprintf(state.path_input, sizeof(state.path_input), "%s", text.c_str());
}

static void set_output_input(AppState& state, const fs::path& path) {
  const std::string text = path.string();
  std::snprintf(state.output_input, sizeof(state.output_input), "%s", text.c_str());
}

static std::string trim_copy(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return text.substr(start, end - start);
}

bool is_supported_image(const fs::path& file_path) {
  if (!file_path.has_extension()) {
    return false;
  }

  std::string ext = file_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".pdf";
}

fs::path parse_user_path_input(const char* raw_input) {
  if (!raw_input) {
    return fs::path();
  }

  std::string text = trim_copy(raw_input);
  if (text.size() >= 2) {
    const char first = text.front();
    const char last = text.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      text = text.substr(1, text.size() - 2);
    }
  }

  return fs::path(text);
}

static fs::path default_output_dir(const AppState& state) {
  if (!state.current_dir.empty()) {
    return state.current_dir / "resized_output";
  }

  return fs::current_path() / "resized_output";
}

const char* output_format_label(int format) {
  switch (format) {
    case IMG_FORMAT_PNG:
      return "PNG";
    case IMG_FORMAT_JPG:
      return "JPG";
    case IMG_FORMAT_PDF:
      return "PDF";
    default:
      return "PNG";
  }
}

static const char* output_format_extension(int format) {
  switch (format) {
    case IMG_FORMAT_PNG:
      return ".png";
    case IMG_FORMAT_JPG:
      return ".jpg";
    case IMG_FORMAT_PDF:
      return ".pdf";
    default:
      return ".png";
  }
}

static fs::path get_output_dir(const AppState& state) {
  const fs::path parsed = parse_user_path_input(state.output_input);
  if (!parsed.empty()) {
    return parsed;
  }

  return default_output_dir(state);
}

static bool ensure_directory_exists(const fs::path& dir, std::string& status) {
  std::error_code ec;
  if (dir.empty()) {
    status = "Output directory is empty.";
    return false;
  }

  if (fs::exists(dir, ec)) {
    if (ec || !fs::is_directory(dir, ec) || ec) {
      status = "Output path is not a directory: " + dir.string();
      return false;
    }

    return true;
  }

  if (!fs::create_directories(dir, ec) && ec) {
    status = "Failed to create output directory: " + dir.string();
    return false;
  }

  return true;
}

void clear_current_image(AppState& state) {
  state.preview_texture.destroy();
  if (state.current_image.pixels) {
    img_free(&state.current_image);
  }
  state.new_width = 0;
  state.new_height = 0;
}

static bool upload_to_texture(GLTexture& texture, const Image& img) {
  texture.destroy();

  glGenTextures(1, &texture.id);
  if (texture.id == 0) {
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, texture.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RGBA,
      img.w,
      img.h,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      img.pixels);

  texture.width = img.w;
  texture.height = img.h;
  return true;
}

void load_path(AppState& state, const fs::path& input_path) {
  load_paths(state, std::vector<fs::path>{input_path});
}

void load_paths(AppState& state, const std::vector<fs::path>& input_paths) {
  state.files.clear();
  state.selected_index = -1;
  state.library_page = 0;
  clear_current_image(state);

  if (input_paths.empty()) {
    state.last_save_succeeded = false;
    state.status = "Path does not exist.";
    return;
  }

  std::set<fs::path> collected;
  bool included_directory = false;
  fs::path first_valid_input;

  for (const fs::path& input_path : input_paths) {
    std::error_code ec;
    if (input_path.empty() || !fs::exists(input_path, ec) || ec) {
      continue;
    }

    if (first_valid_input.empty()) {
      first_valid_input = input_path;
    }

    if (fs::is_directory(input_path, ec) && !ec) {
      included_directory = true;
      for (const auto& entry : fs::directory_iterator(input_path, ec)) {
        if (ec) {
          break;
        }
        if (!entry.is_regular_file(ec) || ec) {
          ec.clear();
          continue;
        }
        if (is_supported_image(entry.path())) {
          collected.insert(entry.path());
        }
      }
      continue;
    }

    ec.clear();
    if (fs::is_regular_file(input_path, ec) && !ec && is_supported_image(input_path)) {
      collected.insert(input_path);
    }
  }

  state.files.assign(collected.begin(), collected.end());
  if (state.files.empty()) {
    state.last_save_succeeded = false;
    state.status = "No supported images were found.";
    return;
  }

  state.current_dir = state.files.front().has_parent_path() ? state.files.front().parent_path() : fs::current_path();
  if (included_directory && input_paths.size() == 1 && fs::is_directory(input_paths.front())) {
    set_path_input(state, input_paths.front());
  } else if (state.files.size() == 1) {
    set_path_input(state, state.files.front());
  } else {
    const std::string summary = std::to_string(state.files.size()) + " files selected";
    std::snprintf(state.path_input, sizeof(state.path_input), "%s", summary.c_str());
  }
  set_output_input(state, default_output_dir(state));
  state.last_save_succeeded = false;
  load_selected_image(state, 0);
  state.status = "Loaded " + std::to_string(state.files.size()) + " image(s). Selected: " + display_name(state.files.front());
}

void load_selected_image(AppState& state, int index) {
  if (index < 0 || index >= static_cast<int>(state.files.size())) {
    return;
  }

  clear_current_image(state);

  Image loaded = {0, 0, 0, nullptr};
  const std::string file_str = state.files[static_cast<size_t>(index)].string();
  const int rc = img_load_rgba(file_str.c_str(), &loaded);
  if (rc != 0) {
    state.last_save_succeeded = false;
    if (rc == -10) {
      state.status = "PDF loading is not supported on this platform yet.";
    } else {
      state.status = "Failed to load image: " + file_str;
    }
    return;
  }

  if (!upload_to_texture(state.preview_texture, loaded)) {
    img_free(&loaded);
    state.last_save_succeeded = false;
    state.status = "Failed to upload OpenGL texture.";
    return;
  }

  state.current_image = loaded;
  state.selected_index = index;
  state.library_page = index / 6;
  state.new_width = loaded.w;
  state.new_height = loaded.h;
  state.last_save_succeeded = false;
  state.status = "Loaded: " + display_name(state.files[static_cast<size_t>(index)]);
}

static fs::path make_output_path(const fs::path& base_dir, const fs::path& src_path, int w, int h, int output_format) {
  const std::string stem = src_path.stem().string();
  const std::string filename =
      stem + "_" + std::to_string(w) + "x" + std::to_string(h) + output_format_extension(output_format);
  return base_dir / filename;
}

bool resize_selected_and_save(AppState& state) {
  if (!state.current_image.pixels || state.selected_index < 0 || state.selected_index >= static_cast<int>(state.files.size())) {
    state.last_save_succeeded = false;
    state.status = "No image selected.";
    return false;
  }

  if (state.new_width <= 0 || state.new_height <= 0) {
    state.last_save_succeeded = false;
    state.status = "Width and Height must be positive.";
    return false;
  }

  Image resized = {0, 0, 0, nullptr};
  const int rc_resize = img_resize_rgba(&state.current_image, state.new_width, state.new_height, &resized);
  if (rc_resize != 0) {
    state.last_save_succeeded = false;
    state.status = "Resize failed.";
    return false;
  }

  const fs::path src = state.files[static_cast<size_t>(state.selected_index)];
  const fs::path output_dir = get_output_dir(state);
  if (!ensure_directory_exists(output_dir, state.status)) {
    img_free(&resized);
    state.last_save_succeeded = false;
    return false;
  }

  const fs::path out = make_output_path(output_dir, src, state.new_width, state.new_height, state.output_format);
  const std::string out_str = out.string();

  const int rc_save = img_save_with_format(
      out_str.c_str(),
      &resized,
      static_cast<ImageFileFormat>(state.output_format),
      state.jpg_quality);
  img_free(&resized);

  if (rc_save != 0) {
    state.last_save_succeeded = false;
    state.status = "Save failed: " + out_str;
    return false;
  }

  state.last_save_succeeded = true;
  state.status = "Saved: " + out.string();
  return true;
}

void resize_all_and_save(AppState& state) {
  if (state.files.empty()) {
    state.last_save_succeeded = false;
    state.status = "No image in the directory.";
    return;
  }

  if (state.new_width <= 0 || state.new_height <= 0) {
    state.last_save_succeeded = false;
    state.status = "Width and Height must be positive.";
    return;
  }

  int success_count = 0;
  int fail_count = 0;
  const fs::path output_dir = get_output_dir(state);
  if (!ensure_directory_exists(output_dir, state.status)) {
    state.last_save_succeeded = false;
    return;
  }

  for (const auto& src : state.files) {
    Image loaded = {0, 0, 0, nullptr};
    const std::string src_str = src.string();

    if (img_load_rgba(src_str.c_str(), &loaded) != 0) {
      ++fail_count;
      continue;
    }

    Image resized = {0, 0, 0, nullptr};
    const int rc_resize = img_resize_rgba(&loaded, state.new_width, state.new_height, &resized);
    img_free(&loaded);

    if (rc_resize != 0) {
      ++fail_count;
      continue;
    }

    const fs::path out = make_output_path(output_dir, src, state.new_width, state.new_height, state.output_format);
    const std::string out_str = out.string();

    if (img_save_with_format(
            out_str.c_str(),
            &resized,
            static_cast<ImageFileFormat>(state.output_format),
            state.jpg_quality) == 0) {
      ++success_count;
    } else {
      ++fail_count;
    }

    img_free(&resized);
  }

  state.last_save_succeeded = (success_count > 0 && fail_count == 0);
  state.status = "Batch resize finished. success=" + std::to_string(success_count) + ", fail=" + std::to_string(fail_count);
}
