#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
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
  char dir_input[1024] = "";
  char output_input[1024] = "";
  fs::path current_dir;
  fs::path output_dir;
  std::vector<fs::path> files;
  int selected_index = -1;

  Image current_image = {0, 0, 0, nullptr};
  GLTexture preview_texture;

  int new_width = 0;
  int new_height = 0;
  std::string status = "Directory is not loaded yet.";
};

static void copy_path_to_input(AppState& state, const fs::path& path) {
  std::snprintf(state.dir_input, sizeof(state.dir_input), "%s", path.string().c_str());
}

static void copy_output_to_input(AppState& state, const fs::path& path) {
  std::snprintf(state.output_input, sizeof(state.output_input), "%s", path.string().c_str());
}

static void set_default_output_dir(AppState& state, const fs::path& base_dir) {
  if (base_dir.empty()) {
    state.output_dir.clear();
    state.output_input[0] = '\0';
    return;
  }

  state.output_dir = base_dir / "resized_output";
  copy_output_to_input(state, state.output_dir);
}

static bool is_supported_image(const fs::path& file_path) {
  if (!file_path.has_extension()) {
    return false;
  }

  std::string ext = file_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}

static bool is_known_but_unsupported_image(const fs::path& file_path) {
  if (!file_path.has_extension()) {
    return false;
  }

  std::string ext = file_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  return ext == ".heic" || ext == ".heif";
}

static void clear_current_image(AppState& state) {
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

static bool ensure_output_directory(AppState& state) {
  fs::path output_dir = fs::path(state.output_input).lexically_normal();
  if (output_dir.empty()) {
    if (state.current_dir.empty()) {
      state.status = "Output directory is empty.";
      return false;
    }
    output_dir = state.current_dir / "resized_output";
  }

  std::error_code ec;
  if (fs::exists(output_dir, ec)) {
    if (ec) {
      state.status = "Failed to access output directory: " + output_dir.string();
      return false;
    }
    if (!fs::is_directory(output_dir, ec) || ec) {
      state.status = "Output path is not a directory: " + output_dir.string();
      return false;
    }
  } else if (!fs::create_directories(output_dir, ec) || ec) {
    state.status = "Failed to create output directory: " + output_dir.string();
    return false;
  }

  state.output_dir = output_dir;
  copy_output_to_input(state, output_dir);
  return true;
}

static void load_directory(AppState& state, const fs::path& dir) {
  state.files.clear();
  state.selected_index = -1;
  clear_current_image(state);

  if (dir.empty()) {
    state.status = "Enter a file or directory path.";
    return;
  }

  if (!fs::exists(dir)) {
    state.status = "Path does not exist: " + dir.string();
    return;
  }

  if (!fs::is_directory(dir)) {
    state.status = "Path is not a directory: " + dir.string();
    return;
  }

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    if (is_supported_image(entry.path())) {
      state.files.push_back(entry.path());
    }
  }

  std::sort(state.files.begin(), state.files.end());
  state.current_dir = dir;
  set_default_output_dir(state, dir);

  if (state.files.empty()) {
    state.status = "No supported images found. Supported: PNG, JPG, JPEG, BMP, TGA.";
    return;
  }

  state.status = "Loaded " + std::to_string(state.files.size()) + " image(s).";
}

static void load_selected_image(AppState& state, int index) {
  if (index < 0 || index >= static_cast<int>(state.files.size())) {
    return;
  }

  clear_current_image(state);

  Image loaded = {0, 0, 0, nullptr};
  const std::string file_str = state.files[static_cast<size_t>(index)].string();
  const int rc = img_load_rgba(file_str.c_str(), &loaded);
  if (rc != 0) {
    state.status = "Failed to load image: " + file_str;
    return;
  }

  if (!upload_to_texture(state.preview_texture, loaded)) {
    img_free(&loaded);
    state.status = "Failed to upload OpenGL texture.";
    return;
  }

  state.current_image = loaded;
  state.selected_index = index;
  state.new_width = loaded.w;
  state.new_height = loaded.h;
  state.status = "Loaded: " + state.files[static_cast<size_t>(index)].filename().string();
}

static void load_path(AppState& state, const fs::path& path) {
  const fs::path normalized = path.lexically_normal();
  copy_path_to_input(state, normalized);

  if (normalized.empty()) {
    state.status = "Enter a file or directory path.";
    return;
  }

  if (!fs::exists(normalized)) {
    state.status = "Path does not exist: " + normalized.string();
    return;
  }

  if (fs::is_directory(normalized)) {
    load_directory(state, normalized);
    return;
  }

  if (!fs::is_regular_file(normalized)) {
    state.status = "Path is not a regular file: " + normalized.string();
    return;
  }

  if (is_known_but_unsupported_image(normalized)) {
    state.status = "HEIC/HEIF is not supported in this version. Use JPG or PNG.";
    return;
  }

  if (!is_supported_image(normalized)) {
    state.status = "Unsupported image format. Supported: PNG, JPG, JPEG, BMP, TGA.";
    return;
  }

  load_directory(state, normalized.parent_path());
  if (state.files.empty()) {
    state.files.push_back(normalized);
    state.current_dir = normalized.parent_path();
    set_default_output_dir(state, state.current_dir);
  }

  for (int i = 0; i < static_cast<int>(state.files.size()); ++i) {
    if (state.files[static_cast<size_t>(i)].lexically_normal() == normalized) {
      load_selected_image(state, i);
      return;
    }
  }

  state.status = "Failed to locate image after loading directory: " + normalized.string();
}

static fs::path make_output_path(const fs::path& base_dir, const fs::path& src_path, int w, int h) {
  const std::string stem = src_path.stem().string();
  const std::string filename = stem + "_" + std::to_string(w) + "x" + std::to_string(h) + ".png";
  return base_dir / filename;
}

static bool resize_selected_and_save(AppState& state) {
  if (!state.current_image.pixels || state.selected_index < 0 || state.selected_index >= static_cast<int>(state.files.size())) {
    state.status = "No image selected.";
    return false;
  }

  if (state.new_width <= 0 || state.new_height <= 0) {
    state.status = "Width and Height must be positive.";
    return false;
  }

  if (!ensure_output_directory(state)) {
    return false;
  }

  Image resized = {0, 0, 0, nullptr};
  const int rc_resize = img_resize_rgba(&state.current_image, state.new_width, state.new_height, &resized);
  if (rc_resize != 0) {
    state.status = "Resize failed.";
    return false;
  }

  const fs::path src = state.files[static_cast<size_t>(state.selected_index)];
  const fs::path out = make_output_path(state.output_dir, src, state.new_width, state.new_height);
  const std::string out_str = out.string();

  const int rc_save = img_save_png(out_str.c_str(), &resized);
  img_free(&resized);

  if (rc_save != 0) {
    state.status = "Save failed: " + out_str;
    return false;
  }

  state.status = "Saved: " + out.string();
  return true;
}

static void resize_all_and_save(AppState& state) {
  if (state.files.empty()) {
    state.status = "No image in the directory.";
    return;
  }

  if (state.new_width <= 0 || state.new_height <= 0) {
    state.status = "Width and Height must be positive.";
    return;
  }

  if (!ensure_output_directory(state)) {
    return;
  }

  int success_count = 0;
  int fail_count = 0;

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

    const fs::path out = make_output_path(state.output_dir, src, state.new_width, state.new_height);
    const std::string out_str = out.string();

    if (img_save_png(out_str.c_str(), &resized) == 0) {
      ++success_count;
    } else {
      ++fail_count;
    }

    img_free(&resized);
  }

  state.status = "Batch resize finished. success=" + std::to_string(success_count) + ", fail=" + std::to_string(fail_count);
}

static void draw_ui(AppState& state) {
  ImGui::Begin("Image Resizer");

  ImGui::TextUnformatted("Path (file or directory)");
  ImGui::PushItemWidth(-120.0f);
  ImGui::InputText("##dir", state.dir_input, IM_ARRAYSIZE(state.dir_input));
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Open")) {
    load_path(state, fs::path(state.dir_input));
  }

  ImGui::TextUnformatted("Output Directory");
  ImGui::InputText("##output_dir", state.output_input, IM_ARRAYSIZE(state.output_input));

  ImGui::Separator();
  ImGui::Columns(2, "mainColumns", true);

  ImGui::TextUnformatted("Images");
  ImGui::BeginChild("fileList", ImVec2(0.0f, -1.0f), true);
  for (int i = 0; i < static_cast<int>(state.files.size()); ++i) {
    const bool selected = (i == state.selected_index);
    const std::string name = state.files[static_cast<size_t>(i)].filename().string();
    if (ImGui::Selectable(name.c_str(), selected)) {
      load_selected_image(state, i);
    }
  }
  ImGui::EndChild();

  ImGui::NextColumn();

  ImGui::TextUnformatted("Preview");
  ImGui::BeginChild("previewPanel", ImVec2(0.0f, 0.0f), true);

  if (state.preview_texture.id != 0) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float max_w = std::max(1.0f, avail.x - 16.0f);
    float max_h = std::max(1.0f, avail.y - 120.0f);

    float scale_w = max_w / static_cast<float>(state.preview_texture.width);
    float scale_h = max_h / static_cast<float>(state.preview_texture.height);
    float scale = std::min(1.0f, std::min(scale_w, scale_h));

    const ImVec2 size(
        state.preview_texture.width * scale,
        state.preview_texture.height * scale);

    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(state.preview_texture.id)), size);
  } else {
    ImGui::TextUnformatted("No image selected.");
  }

  ImGui::InputInt("Width", &state.new_width);
  ImGui::InputInt("Height", &state.new_height);

  if (ImGui::Button("Resize Selected")) {
    resize_selected_and_save(state);
  }

  ImGui::SameLine();

  if (ImGui::Button("Resize All")) {
    resize_all_and_save(state);
  }

  ImGui::EndChild();

  ImGui::Columns(1);
  ImGui::Separator();
  ImGui::TextWrapped("Status: %s", state.status.c_str());

  ImGui::End();
}

static void glfw_error_callback(int error, const char* description) {
  (void)error;
  (void)description;
}

static void glfw_drop_callback(GLFWwindow* window, int path_count, const char* paths[]) {
  if (!window || path_count <= 0 || !paths || !paths[0]) {
    return;
  }

  AppState* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (!state) {
    return;
  }

  load_path(*state, fs::path(paths[0]));
}

int main() {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    return 1;
  }

#if defined(__APPLE__)
  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  GLFWwindow* window = glfwCreateWindow(1280, 800, "ImageTool", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  AppState state;
  glfwSetWindowUserPointer(window, &state);
  glfwSetDropCallback(window, glfw_drop_callback);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    draw_ui(state);

    ImGui::Render();

    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    glViewport(0, 0, display_w, display_h);
    glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  clear_current_image(state);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
