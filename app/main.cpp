// エントリポイントです。
// Application オブジェクトを生成して run() を呼ぶだけにとどめ、
// 初期化・ループ・終了処理の詳細は application.cpp に委ねます。
#include "application.h"

int main() {
  // Application のコンストラクタが GLFW と ImGui を初期化し、
  // run() がメインループを回し、デストラクタがクリーンアップを行います。
  Application app;
  return app.run();
}
