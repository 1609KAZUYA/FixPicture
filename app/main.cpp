#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
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
  fs::path current_dir;
  std::vector<fs::path> files;
  int selected_index = -1;

  Image current_image = {0, 0, 0, nullptr};
  GLTexture preview_texture;

  int new_width = 0;
  int new_height = 0;
  std::string status = "Directory is not loaded yet.";
};

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

static void load_directory(AppState& state, const fs::path& dir) {
  state.files.clear();
  state.selected_index = -1;
  clear_current_image(state);

  if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
    state.status = "Directory does not exist.";
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

  Image resized = {0, 0, 0, nullptr};
  const int rc_resize = img_resize_rgba(&state.current_image, state.new_width, state.new_height, &resized);
  if (rc_resize != 0) {
    state.status = "Resize failed.";
    return false;
  }

  const fs::path src = state.files[static_cast<size_t>(state.selected_index)];
  const fs::path out = make_output_path(state.current_dir, src, state.new_width, state.new_height);
  const std::string out_str = out.string();

  const int rc_save = img_save_png(out_str.c_str(), &resized);
  img_free(&resized);

  if (rc_save != 0) {
    state.status = "Save failed: " + out_str;
    return false;
  }

  state.status = "Saved: " + out.filename().string();
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

    const fs::path out = make_output_path(state.current_dir, src, state.new_width, state.new_height);
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

  ImGui::TextUnformatted("Directory");
  ImGui::PushItemWidth(-120.0f);
  ImGui::InputText("##dir", state.dir_input, IM_ARRAYSIZE(state.dir_input));
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Load")) {
    load_directory(state, fs::path(state.dir_input));
  }

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

    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(state.preview_texture.id)), size);
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
