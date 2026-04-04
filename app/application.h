#pragma once

#include "app_state.h"

struct GLFWwindow;

class Application {
 public:
  Application();
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

 private:
  void init_glfw();
  void init_imgui();
  void render_frame();
  void shutdown();

  static void glfw_error_callback(int error, const char* description);
  static void drop_callback(GLFWwindow* window, int count, const char** paths);

  GLFWwindow* window_ = nullptr;
  const char* glsl_version_ = nullptr;
  AppState state_;
};
