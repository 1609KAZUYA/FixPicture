// Application クラスのインターフェース宣言です。
// GLFW ウィンドウと ImGui コンテキストのライフサイクル全体を管理します。
// main.cpp はこのヘッダだけをインクルードし、詳細な依存ヘッダ（GLFW, ImGui）は
// application.cpp 側に隠蔽します。
#pragma once

#include "app_state.h"

// GLFWwindow の前方宣言です。
// このヘッダに <GLFW/glfw3.h> を不要にし、インクルード依存を最小化します。
struct GLFWwindow;

// ウィンドウ生成から終了処理までを担う RAII クラスです。
// コンストラクタで GLFW/ImGui を初期化し、デストラクタでリソースを解放します。
// OS・GPU リソースを所有するためコピーとムーブは禁止しています。
class Application {
 public:
  // GLFW を初期化し、ウィンドウを生成して ImGui をセットアップします。
  // いずれかのステップが失敗した場合 window_ が nullptr のままになり、
  // run() が即座に 1 を返します。
  Application();

  // ImGui と GLFW を安全な順序でシャットダウンします。
  ~Application();

  // コピー・ムーブは禁止です（GLFW ウィンドウと OpenGL リソースを直接所有するため）。
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // メインループを実行します。
  // ウィンドウが閉じられるまでポーリングと描画を繰り返し、
  // 終了後のリターンコードを返します（成功: 0 / 初期化失敗: 1）。
  int run();

 private:
  // GLFW の初期化、プラットフォームごとの OpenGL バージョン設定、
  // ウィンドウ生成、ドロップコールバック登録を行います。
  void init_glfw();

  // ImGui コンテキスト生成、フォント設定、テーマ適用、
  // バックエンド（GLFW + OpenGL3）の初期化を行います。
  // init_glfw() の後で呼ぶ必要があります。
  void init_imgui();

  // ImGui の描画データを OpenGL へ転送し、バッファをスワップします。
  // run() のループ末尾から毎フレーム呼ばれます。
  void render_frame();

  // ImGui バックエンド、ImGui コンテキスト、GLFW ウィンドウを
  // 安全な順序で解放します。デストラクタから呼ばれます。
  void shutdown();

  // GLFW が要求するエラーコールバックです（static 必須）。
  // 現在はエラーを無視しますが、将来ロギング実装を追加できます。
  static void glfw_error_callback(int error, const char* description);

  // ファイルやフォルダをウィンドウにドロップしたときに GLFW から呼ばれます（static 必須）。
  // UserPointer 経由で AppState を取得し、append_paths() へ転送します。
  static void drop_callback(GLFWwindow* window, int count, const char** paths);

  // 生成した GLFW ウィンドウへのポインタです。
  // nullptr の場合は初期化失敗を意味します。
  GLFWwindow* window_ = nullptr;

  // プラットフォームに応じた GLSL バージョン文字列です。
  // macOS: "#version 150" / Windows・Linux: "#version 130"
  const char* glsl_version_ = nullptr;

  // アプリケーション全体の状態を保持する唯一の構造体です。
  // ドロップコールバックから参照するため UserPointer へも登録します。
  AppState state_;
};
