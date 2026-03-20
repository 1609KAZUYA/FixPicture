#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "image_core.h"

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#  include <commdlg.h>
#  include <objbase.h>
#  include <shlobj.h>
#endif

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

  Image current_image = {0, 0, 0, nullptr};
  GLTexture preview_texture;

  int new_width = 0;
  int new_height = 0;
  int output_format = IMG_FORMAT_PNG;
  int jpg_quality = 90;
  bool last_save_succeeded = false;
  std::string status = "No file or directory loaded yet.";
};

static bool is_supported_image(const fs::path& file_path) {
  if (!file_path.has_extension()) {
    return false;
  }

  std::string ext = file_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".pdf";
}

static void load_selected_image(AppState& state, int index);

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

static fs::path parse_user_path_input(const char* raw_input) {
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

#if defined(_WIN32)
static std::string wide_to_utf8(const wchar_t* value) {
  if (!value || value[0] == L'\0') {
    return std::string();
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return std::string();
  }

  std::string result(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  return result;
}

static bool prompt_open_image_file(fs::path& out_path) {
  static const wchar_t kFilter[] =
      L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.pdf\0All Files\0*.*\0";
  wchar_t buffer[4096] = L"";
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = nullptr;
  dialog.lpstrFilter = kFilter;
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  dialog.lpstrTitle = L"Select an image";

  if (!GetOpenFileNameW(&dialog)) {
    return false;
  }

  out_path = fs::path(wide_to_utf8(buffer));
  return !out_path.empty();
}

static bool prompt_select_directory(fs::path& out_path, const wchar_t* title) {
  HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool should_uninit = SUCCEEDED(init_result);

  BROWSEINFOW dialog = {};
  dialog.lpszTitle = title;
  dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&dialog);
  if (!item) {
    if (should_uninit) {
      CoUninitialize();
    }
    return false;
  }

  wchar_t buffer[MAX_PATH] = L"";
  const BOOL ok = SHGetPathFromIDListW(item, buffer);
  CoTaskMemFree(item);

  if (should_uninit) {
    CoUninitialize();
  }

  if (!ok) {
    return false;
  }

  out_path = fs::path(wide_to_utf8(buffer));
  return !out_path.empty();
}
#elif defined(__APPLE__)
static std::string run_command_and_capture(const char* command) {
  if (!command) {
    return std::string();
  }

  FILE* pipe = popen(command, "r");
  if (!pipe) {
    return std::string();
  }

  std::string output;
  char buffer[512];
  while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    output += buffer;
  }
  pclose(pipe);
  return trim_copy(output);
}

static bool prompt_open_image_file(fs::path& out_path) {
  const std::string result = run_command_and_capture(
      "osascript -e 'POSIX path of (choose file with prompt \"Select an image\")'");
  if (result.empty()) {
    return false;
  }

  out_path = fs::path(result);
  return true;
}

static bool prompt_select_directory(fs::path& out_path, const char* title) {
  const std::string prompt = title ? title : "Select a folder";
  const std::string command =
      "osascript -e 'POSIX path of (choose folder with prompt \"" + prompt + "\")'";
  const std::string result = run_command_and_capture(command.c_str());
  if (result.empty()) {
    return false;
  }

  out_path = fs::path(result);
  return true;
}
#else
static bool prompt_open_image_file(fs::path& out_path) {
  (void)out_path;
  return false;
}

static bool prompt_select_directory(fs::path& out_path, const char* title) {
  (void)out_path;
  (void)title;
  return false;
}
#endif

static fs::path default_output_dir(const AppState& state) {
  if (!state.current_dir.empty()) {
    return state.current_dir / "resized_output";
  }

  return fs::current_path() / "resized_output";
}

static const char* output_format_label(int format) {
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

  std::error_code ec;
  if (dir.empty() || !fs::exists(dir, ec) || ec || !fs::is_directory(dir, ec) || ec) {
    state.last_save_succeeded = false;
    state.status = "Directory does not exist.";
    return;
  }

  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec) {
      state.last_save_succeeded = false;
      state.status = "Failed to enumerate the directory.";
      state.files.clear();
      return;
    }

    if (!entry.is_regular_file(ec) || ec) {
      ec.clear();
      continue;
    }

    if (is_supported_image(entry.path())) {
      state.files.push_back(entry.path());
    }
  }

  std::sort(state.files.begin(), state.files.end());
  state.current_dir = dir;
  set_path_input(state, dir);
  set_output_input(state, default_output_dir(state));

  if (state.files.empty()) {
    state.last_save_succeeded = false;
    state.status = "No supported images were found in the directory.";
    return;
  }

  state.last_save_succeeded = false;
  state.status = "Loaded " + std::to_string(state.files.size()) + " image(s).";
  load_selected_image(state, 0);
}

static void load_path(AppState& state, const fs::path& input_path) {
  state.files.clear();
  state.selected_index = -1;
  clear_current_image(state);

  std::error_code ec;
  if (input_path.empty() || !fs::exists(input_path, ec) || ec) {
    state.last_save_succeeded = false;
    state.status = "Path does not exist.";
    return;
  }

  set_path_input(state, input_path);

  if (fs::is_directory(input_path, ec) && !ec) {
    load_directory(state, input_path);
    return;
  }

  ec.clear();
  if (!fs::is_regular_file(input_path, ec) || ec) {
    state.last_save_succeeded = false;
    state.status = "The path is neither a readable file nor a directory.";
    return;
  }

  if (!is_supported_image(input_path)) {
    state.last_save_succeeded = false;
    state.status = "Unsupported image format.";
    return;
  }

  state.files.push_back(input_path);
  state.current_dir = input_path.has_parent_path() ? input_path.parent_path() : fs::current_path();
  set_output_input(state, default_output_dir(state));
  state.last_save_succeeded = false;
  state.status = "Loaded 1 image.";
  load_selected_image(state, 0);
}

static void drop_callback(GLFWwindow* window, int count, const char** paths) {
  if (!window || count <= 0 || !paths || !paths[0]) {
    return;
  }

  AppState* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (!state) {
    return;
  }

  load_path(*state, fs::path(paths[0]));
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
  state.new_width = loaded.w;
  state.new_height = loaded.h;
  state.last_save_succeeded = false;
  state.status = "Loaded: " + state.files[static_cast<size_t>(index)].filename().string();
}

static fs::path make_output_path(const fs::path& base_dir, const fs::path& src_path, int w, int h, int output_format) {
  const std::string stem = src_path.stem().string();
  const std::string filename =
      stem + "_" + std::to_string(w) + "x" + std::to_string(h) + output_format_extension(output_format);
  return base_dir / filename;
}

static bool resize_selected_and_save(AppState& state) {
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

static void resize_all_and_save(AppState& state) {
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

static void draw_ui(AppState& state) {
  ImGui::Begin("Image Resizer");

  ImGui::TextUnformatted("Path (file or directory)");
  ImGui::PushItemWidth(-1.0f);
  ImGui::InputText("##path", state.path_input, IM_ARRAYSIZE(state.path_input));
  ImGui::PopItemWidth();

  if (ImGui::Button("Open File...")) {
    fs::path selected_path;
    if (prompt_open_image_file(selected_path)) {
      load_path(state, selected_path);
    }
  }

  ImGui::SameLine();

  if (ImGui::Button("Open Folder...")) {
    fs::path selected_path;
#if defined(_WIN32)
    if (prompt_select_directory(selected_path, L"Select an image folder")) {
#else
    if (prompt_select_directory(selected_path, "Select an image folder")) {
#endif
      load_path(state, selected_path);
    }
  }

  ImGui::SameLine();

  if (ImGui::Button("Load Path")) {
    load_path(state, parse_user_path_input(state.path_input));
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

    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(state.preview_texture.id)), size);
  } else {
    ImGui::TextUnformatted("No image selected.");
  }

  ImGui::InputText("Output Directory", state.output_input, IM_ARRAYSIZE(state.output_input));
  ImGui::SameLine();
  if (ImGui::Button("Choose...")) {
    fs::path selected_path;
#if defined(_WIN32)
    if (prompt_select_directory(selected_path, L"Select output folder")) {
#else
    if (prompt_select_directory(selected_path, "Select output folder")) {
#endif
      set_output_input(state, selected_path);
    }
  }
  ImGui::SetNextItemWidth(140.0f);
  if (ImGui::BeginCombo("Format", output_format_label(state.output_format))) {
    for (int format : {IMG_FORMAT_PNG, IMG_FORMAT_JPG, IMG_FORMAT_PDF}) {
      const bool selected = (state.output_format == format);
      if (ImGui::Selectable(output_format_label(format), selected)) {
        state.output_format = format;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  if (state.output_format == IMG_FORMAT_JPG || state.output_format == IMG_FORMAT_PDF) {
    ImGui::SliderInt("Quality", &state.jpg_quality, 1, 100);
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
  if (state.last_save_succeeded) {
    ImGui::TextColored(ImVec4(0.20f, 0.78f, 0.35f, 1.0f), "Save completed successfully.");
  }
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
  glfwSetWindowUserPointer(window, &state);
  glfwSetDropCallback(window, drop_callback);

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
