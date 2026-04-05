// Application クラスの実装です。
// GLFW/ImGui の初期化・メインループ・シャットダウン・コールバック処理を担います。
// 詳細な依存ヘッダはこのファイル内に閉じ込め、application.h をシンプルに保ちます。
#include "application.h"

#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <vector>

#include "image_session.h"
#include "ui.h"

// コンストラクタ: GLFW → ImGui の順に初期化します。
// 依存関係があるため必ずこの順序を守ります。
Application::Application() {
  init_glfw();
  init_imgui();
}

// デストラクタ: shutdown() を呼んでリソースを解放します。
// RAII により、例外や早期 return が発生しても必ず実行されます。
Application::~Application() {
  shutdown();
}

void Application::init_glfw() {
  // GLFW 内部エラーをフックし、デバッグ情報を取得できるようにします。
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    // 初期化失敗時は window_ を nullptr のままにします。
    // run() 側で nullptr を検出してエラーコードを返します。
    return;
  }

  // macOS は Core Profile 3.2 / GLSL 150 を使用します。
  // Windows・Linux は Compatibility Profile 3.0 / GLSL 130 を使用します。
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

  // OpenGL コンテキストをこのスレッドへ紐付け、垂直同期を有効にします。
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);

  // ドロップコールバック内で AppState を参照できるよう UserPointer へ登録します。
  glfwSetWindowUserPointer(window_, &state_);
  glfwSetDropCallback(window_, drop_callback);
}

void Application::init_imgui() {
  // ウィンドウ生成に失敗した場合は ImGui 初期化をスキップします。
  if (!window_) {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  // プラットフォームに応じたフォントを読み込みます（日本語グリフのマージを含む）。
  configure_ui_fonts(io);
  io.FontGlobalScale = 1.0f;

  // アプリ固有のカラーテーマ（パープル/シアン系）を適用します。
  apply_ui_theme();

  // GLFW と OpenGL3 バックエンドを接続します。
  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(glsl_version_);
}

int Application::run() {
  // 初期化失敗（window_ が nullptr）の場合は即座にエラーを返します。
  if (!window_) {
    return 1;
  }

  // ウィンドウが閉じられるまでフレームを繰り返します。
  while (!glfwWindowShouldClose(window_)) {
    // OS イベント（入力・リサイズなど）を処理します。
    glfwPollEvents();

    // ImGui の新規フレームを開始します（バックエンド → ImGui の順）。
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // アプリの UI ウィジェットをすべて描画します（実際の描画コマンドは Render() 後）。
    draw_ui(state_);

    // ImGui の描画コマンドリストを確定します。
    ImGui::Render();

    // OpenGL へ転送してバッファをスワップします。
    render_frame();
  }

  // ループを抜けた後、画像データと OpenGL テクスチャを解放します。
  clear_current_image(state_);
  return 0;
}

void Application::render_frame() {
  // フレームバッファサイズを取得し、DPI 変化にも対応します。
  int display_w = 0;
  int display_h = 0;
  glfwGetFramebufferSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  // 背景色（ほぼ黒の深い紺）でバッファをクリアします。
  glClearColor(0.02f, 0.04f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // 確定した ImGui 描画コマンドを OpenGL で実行します。
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // フロント/バックバッファを入れ替えて画面に表示します。
  glfwSwapBuffers(window_);
}

void Application::shutdown() {
  // window_ が nullptr の場合は初期化に失敗しているのでスキップします。
  if (!window_) {
    return;
  }

  // ImGui → GLFW の順にシャットダウンします（初期化と逆順）。
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window_);
  glfwTerminate();
  // 二重解放を防ぐために nullptr へ戻します。
  window_ = nullptr;
}

// GLFW エラーコールバック（static）。
// 現在はエラーを無視します。将来的にはここでログ出力を追加できます。
void Application::glfw_error_callback(int error, const char* description) {
  (void)error;
  (void)description;
}

// ドロップコールバック（static）。
// GLFW から渡された生パス配列を fs::path のベクターへ変換し、
// append_paths() を通じてファイルリストへ追加します。
void Application::drop_callback(GLFWwindow* window, int count, const char** paths) {
  if (!window || count <= 0 || !paths || !paths[0]) {
    return;
  }

  // UserPointer に格納した AppState を取り出します。
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

  // 既存ファイルリストへ追加します（重複は image_session 側で排除）。
  append_paths(*state, input_paths);
}
