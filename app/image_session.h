// 画像セッション管理のインターフェース宣言です。
// ファイルの読み込み・追加・画像選択・リサイズ保存など、
// ビジネスロジックに相当する操作を提供します。
// UI 層（ui.cpp）とコールバック（application.cpp）から使います。
#pragma once

#include "app_state.h"

// ---- ユーティリティ --------------------------------------------------

// ファイルパスが対応画像形式かどうかを判定します。
// 対応形式: PNG / JPG / JPEG / BMP / TGA / PDF（全プラットフォーム）
//           HEIC / HEIF（macOS のみ）
bool is_supported_image(const fs::path& file_path);

// ユーザー入力の生文字列をファイルシステムパスへ変換します。
// 前後の空白やクォートを除去してから fs::path を返します。
fs::path parse_user_path_input(const char* raw_input);

// 出力フォーマット定数（IMG_FORMAT_*）に対応する表示名を返します。
// コンボボックスのラベルや保存確認メッセージに使います。
const char* output_format_label(int format);

// ファイルパスから UI 表示用のファイル名文字列を返します。
// macOS では NFD 正規化（分解型 → 合成型）を行い、濁音などを正しく表示します。
std::string display_name(const fs::path& path);

// ---- ファイルリスト操作 ----------------------------------------------

// 指定パス（ファイルまたはディレクトリ）を新規リストとして読み込みます。
// 既存のファイルリストと選択状態はリセットされます。
void load_path(AppState& state, const fs::path& input_path);

// 複数パスを新規リストとして読み込みます。load_path() の複数パス版です。
void load_paths(AppState& state, const std::vector<fs::path>& input_paths);

// 複数パスを既存リストへ追加します（重複は除外）。
// ドロップ操作や「Open File/Folder」ボタンから呼ばれます。
void append_paths(AppState& state, const std::vector<fs::path>& input_paths);

// ---- 画像操作 --------------------------------------------------------

// files[index] の画像をデコードして current_image へ格納し、
// OpenGL テクスチャを更新してプレビューを表示します。
// サイズ入力欄（new_width / new_height）も元画像のサイズで初期化します。
void load_selected_image(AppState& state, int index);

// current_image のピクセルデータと OpenGL テクスチャを解放します。
// サイズ入力欄も 0 にリセットします。
void clear_current_image(AppState& state);

// 選択中の画像を new_width × new_height にリサイズして出力ディレクトリへ保存します。
// 成功した場合 true を返し、state.status を更新します。
bool resize_selected_and_save(AppState& state);

// ファイルリスト内のすべての画像を new_width × new_height にリサイズして保存します。
// 成功数・失敗数を state.status に報告します。
void resize_all_and_save(AppState& state);
