// アプリケーション全体で共有する状態をひとつの構造体にまとめています。
// UI 描画・セッション管理・ファイル操作のすべてがこの構造体を介してデータをやり取りします。
#pragma once

#include <GLFW/glfw3.h>

#include <filesystem>
#include <string>
#include <vector>

#include "image_core.h"

namespace fs = std::filesystem;

// OpenGL テクスチャのハンドルと付随するメタデータを保持します。
// プレビュー表示に使われ、画像が切り替わるたびに destroy() → 再生成されます。
struct GLTexture {
  // OpenGL が割り当てたテクスチャ ID です。0 は「未生成」を意味します。
  GLuint id = 0;
  // テクスチャの横幅（ピクセル単位）です。
  int width = 0;
  // テクスチャの縦幅（ピクセル単位）です。
  int height = 0;

  // テクスチャを GPU から削除し、フィールドを初期値へ戻します。
  // 新しい画像を読み込む前や、アプリ終了時に呼びます。
  void destroy() {
    if (id != 0) {
      glDeleteTextures(1, &id);
      id = 0;
    }
    width = 0;
    height = 0;
  }
};

// アプリ全体の可変状態を管理する中心的な構造体です。
// この構造体のインスタンスは Application クラスが一つだけ持ちます。
struct AppState {
  // ファイルブラウザのパス入力欄に表示する文字列バッファです。
  // 単一ファイル選択時はそのパス、複数の場合は "N files loaded" の形式で表示します。
  char path_input[1024] = "";

  // 出力フォルダのパスをユーザーが手入力または選択して格納するバッファです。
  char output_input[1024] = "";

  // 現在ロードされているファイル群の親ディレクトリです。
  // デフォルト出力先の計算に使います。
  fs::path current_dir;

  // ロード済みの画像ファイルパス一覧です。
  // ライブラリパネルに表示し、selected_index で選択中の要素を管理します。
  std::vector<fs::path> files;

  // files の中で現在選択されているインデックスです。
  // -1 は「未選択」を意味します。
  int selected_index = -1;

  // ライブラリパネルのページネーション用カレントページ番号（0 始まり）です。
  int library_page = 0;

  // 現在プレビュー中のデコード済み画像データです。
  // pixels が nullptr の場合は画像未ロード状態です。
  Image current_image = {0, 0, 0, nullptr};

  // current_image を GPU へアップロードした OpenGL テクスチャです。
  // ImGui の Image() ウィジェットでプレビュー表示に使います。
  GLTexture preview_texture;

  // リサイズ後の目標幅（ピクセル単位）です。UI の入力欄と連動します。
  int new_width = 0;

  // リサイズ後の目標高さ（ピクセル単位）です。UI の入力欄と連動します。
  int new_height = 0;

  // 出力フォーマットです（IMG_FORMAT_PNG / IMG_FORMAT_JPG / IMG_FORMAT_PDF）。
  int output_format = IMG_FORMAT_PNG;

  // JPEG・PDF 保存時の品質値です（1〜100）。
  int jpg_quality = 90;

  // 直前の保存操作が成功したかどうかを示すフラグです。
  // true なら「Saved」、false なら「Status」とラベル色を変えて表示します。
  bool last_save_succeeded = false;

  // ステータスバーに表示するメッセージ文字列です。
  // 操作結果やエラーの簡易説明を格納します。
  std::string status = "No file or directory loaded yet.";
};
