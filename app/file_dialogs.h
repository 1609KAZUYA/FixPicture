// ネイティブファイルダイアログのインターフェース宣言です。
// Windows は Win32 API（GetOpenFileNameW / SHBrowseForFolderW）、
// macOS は osascript（AppleScript）を使って実装しています。
// その他のプラットフォームはスタブ実装で常に false を返します。
#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// 画像ファイルを 1 つ選択するダイアログを表示します。
// ユーザーが選択した場合は out_path に格納して true を返します。
bool prompt_open_image_file(fs::path& out_path);

// 画像ファイルを複数選択できるダイアログを表示します。
// ユーザーが選択した場合は out_paths に格納して true を返します。
bool prompt_open_image_files(std::vector<fs::path>& out_paths);

// 入力元フォルダを選択するダイアログを表示します。
// ユーザーが選択した場合は out_path に格納して true を返します。
bool prompt_select_image_directory(fs::path& out_path);

// 出力先フォルダを選択するダイアログを表示します。
// ユーザーが選択した場合は out_path に格納して true を返します。
bool prompt_select_output_directory(fs::path& out_path);
