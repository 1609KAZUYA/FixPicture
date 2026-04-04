#include "application.h"

#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <vector>

#include "image_session.h"
#include "ui.h"

Application::Application() {
  init_glfw();
  init_imgui();
}

Application::~Application() {
  shutdown();
}

void Application::init_glfw() {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    return;
  }

#if defined(__APPLE__)
  glsl_version_ = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  glsl_version_ = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  window_ = glfwCreateWindow(1280, 800, "ImageTool", nullptr, nullptr);
  if (!window_) {
    glfwTerminate();
    return;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window_, &state_);
  glfwSetDropCallback(window_, drop_callback);
}

void Application::init_imgui() {
  if (!window_) {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  configure_ui_fonts(io);
  io.FontGlobalScale = 1.0f;

  apply_ui_theme();

  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(glsl_version_);
}

int Application::run() {
  if (!window_) {
    return 1;
  }

  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    draw_ui(state_);

    ImGui::Render();
    render_frame();
  }

  clear_current_image(state_);
  return 0;
}

void Application::render_frame() {
  int display_w = 0;
  int display_h = 0;
  glfwGetFramebufferSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  glClearColor(0.02f, 0.04f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window_);
}

void Application::shutdown() {
  if (!window_) {
    return;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window_);
  glfwTerminate();
  window_ = nullptr;
}

void Application::glfw_error_callback(int error, const char* description) {
  (void)error;
  (void)description;
}

void Application::drop_callback(GLFWwindow* window, int count, const char** paths) {
  if (!window || count <= 0 || !paths || !paths[0]) {
    return;
  }

  AppState* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (!state) {
    return;
  }

  std::vector<fs::path> input_paths;
  input_paths.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    if (paths[i]) {
      input_paths.emplace_back(paths[i]);
    }
  }

  append_paths(*state, input_paths);
}
