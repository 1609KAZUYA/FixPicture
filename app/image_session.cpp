// 画像セッション管理の実装です。
// ファイルリストの構築・画像のデコード・OpenGL テクスチャへのアップロード・
// リサイズ保存・出力ディレクトリ管理を担います。
// UI 描画（ui.cpp）とは分離し、状態変更のみ担当します。
#include "image_session.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <set>
#include <vector>

#if defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#endif

// ---- ファイル名正規化 ------------------------------------------------

// ファイルパスから UI 表示用のファイル名を返します。
// macOS では HFS+ が NFD（分解型 Unicode）でファイル名を保存するため、
// CoreFoundation の CFStringNormalize を使って NFC（合成型）へ変換します。
// これにより「が」などの濁音が 1 文字として正しく表示されます。
std::string display_name(const fs::path& path) {
#if defined(__cpp_lib_char8_t)
  const auto u8 = path.filename().u8string();
  std::string utf8(u8.begin(), u8.end());
#else
  std::string utf8 = path.filename().u8string();
#endif

#if defined(__APPLE__)
  // CFString に変換して NFD → NFC 正規化を行います。
  CFStringRef source = CFStringCreateWithCString(kCFAllocatorDefault, utf8.c_str(), kCFStringEncodingUTF8);
  if (!source) {
    return utf8;
  }

  CFMutableStringRef normalized = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, source);
  CFRelease(source);
  if (!normalized) {
    return utf8;
  }

  CFStringNormalize(normalized, kCFStringNormalizationFormC);

  const CFIndex length = CFStringGetLength(normalized);
  const CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string result(static_cast<size_t>(max_size), '\0');
  if (CFStringGetCString(normalized, result.data(), max_size, kCFStringEncodingUTF8)) {
    result.resize(std::strlen(result.c_str()));
    CFRelease(normalized);
    return result;
  }

  CFRelease(normalized);
#endif

  return utf8;
}

// ---- 内部ユーティリティ ----------------------------------------------

// AppState の path_input（ソース表示欄）を指定パスの文字列で上書きします。
static void set_path_input(AppState& state, const fs::path& path) {
  const std::string text = path.string();
  std::snprintf(state.path_input, sizeof(state.path_input), "%s", text.c_str());
}

// AppState の output_input（出力フォルダ欄）を指定パスの文字列で上書きします。
static void set_output_input(AppState& state, const fs::path& path) {
  const std::string text = path.string();
  std::snprintf(state.output_input, sizeof(state.output_input), "%s", text.c_str());
}

// 文字列の前後にある空白文字を取り除いたコピーを返します。
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

// ---- 対応形式判定 ----------------------------------------------------

bool is_supported_image(const fs::path& file_path) {
  if (!file_path.has_extension()) {
    return false;
  }

  // 拡張子を小文字に統一してから比較します。
  std::string ext = file_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  const bool common_supported =
      ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".pdf";
#if defined(__APPLE__)
  // macOS は ImageIO 経由で HEIC / HEIF を読み込めます。
  return common_supported || ext == ".heic" || ext == ".heif";
#else
  return common_supported;
#endif
}

// ---- パス入力パース --------------------------------------------------

fs::path parse_user_path_input(const char* raw_input) {
  if (!raw_input) {
    return fs::path();
  }

  std::string text = trim_copy(raw_input);
  // ダブルクォートまたはシングルクォートで囲まれている場合は除去します。
  if (text.size() >= 2) {
    const char first = text.front();
    const char last = text.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      text = text.substr(1, text.size() - 2);
    }
  }

  return fs::path(text);
}

// ---- 出力ディレクトリ ------------------------------------------------

// output_input が空の場合に使うデフォルト出力先を返します。
// current_dir が設定されていればその下の "resized_output"、
// そうでなければカレントディレクトリの下を返します。
static fs::path default_output_dir(const AppState& state) {
  if (!state.current_dir.empty()) {
    return state.current_dir / "resized_output";
  }

  return fs::current_path() / "resized_output";
}

const char* output_format_label(int format) {
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

// 出力フォーマットに対応するファイル拡張子を返します。
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

// ユーザー入力の output_input を解析して出力ディレクトリを決定します。
// 入力が空の場合は default_output_dir() を使います。
static fs::path get_output_dir(const AppState& state) {
  const fs::path parsed = parse_user_path_input(state.output_input);
  if (!parsed.empty()) {
    return parsed;
  }

  return default_output_dir(state);
}

// 指定ディレクトリが存在しない場合は再帰的に作成します。
// 成功した場合は true、失敗した場合は state.status にエラーメッセージを設定して false を返します。
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

// ---- 画像クリア ------------------------------------------------------

void clear_current_image(AppState& state) {
  // OpenGL テクスチャを GPU から削除します。
  state.preview_texture.destroy();
  // ピクセルバッファを解放します（stbi_image_free 相当）。
  if (state.current_image.pixels) {
    img_free(&state.current_image);
  }
  state.new_width = 0;
  state.new_height = 0;
}

// ---- OpenGL テクスチャアップロード ------------------------------------

// デコード済み Image を OpenGL テクスチャへアップロードします。
// 成功した場合は texture.id が 0 でない値になります。
// 既存テクスチャがある場合は destroy() して再生成します。
static bool upload_to_texture(GLTexture& texture, const Image& img) {
  texture.destroy();

  glGenTextures(1, &texture.id);
  if (texture.id == 0) {
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, texture.id);
  // 線形補間でスムーズに拡縮します。
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // テクスチャ境界でのサンプリングを端のピクセル色に固定します。
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

// ---- パス収集 --------------------------------------------------------

// input_paths の各要素を走査し、対応画像ファイルのパスをまとめて返します。
// ディレクトリが含まれる場合はその中身を再帰せず 1 階層だけ展開します。
// 重複は set で除外し、ディレクトリ内ファイルはソートして追加します。
static std::vector<fs::path> collect_supported_paths(
    const std::vector<fs::path>& input_paths,
    bool& included_directory,
    fs::path& first_valid_input) {
  std::vector<fs::path> collected;
  std::set<fs::path> seen;
  for (const fs::path& input_path : input_paths) {
    std::error_code ec;
    if (input_path.empty() || !fs::exists(input_path, ec) || ec) {
      continue;
    }

    if (first_valid_input.empty()) {
      first_valid_input = input_path;
    }

    if (fs::is_directory(input_path, ec) && !ec) {
      included_directory = true;
      std::vector<fs::path> directory_items;
      for (const auto& entry : fs::directory_iterator(input_path, ec)) {
        if (ec) {
          break;
        }
        if (!entry.is_regular_file(ec) || ec) {
          ec.clear();
          continue;
        }
        if (is_supported_image(entry.path())) {
          directory_items.push_back(entry.path());
        }
      }
      // ディレクトリ内ファイルをパスでソートして一貫した順序にします。
      std::sort(directory_items.begin(), directory_items.end());
      for (const auto& item : directory_items) {
        if (seen.insert(item).second) {
          collected.push_back(item);
        }
      }
      continue;
    }

    ec.clear();
    if (fs::is_regular_file(input_path, ec) && !ec && is_supported_image(input_path)) {
      if (seen.insert(input_path).second) {
        collected.push_back(input_path);
      }
    }
  }

  return collected;
}

// ---- パス表示更新 ----------------------------------------------------

// ファイルリストの件数に応じて path_input の表示内容を更新します。
// 0件: 空文字、1件: フルパス、2件以上: "N files loaded" の要約
static void update_source_summary(AppState& state) {
  if (state.files.empty()) {
    std::snprintf(state.path_input, sizeof(state.path_input), "%s", "");
    return;
  }

  if (state.files.size() == 1) {
    set_path_input(state, state.files.front());
    return;
  }

  const std::string summary = std::to_string(state.files.size()) + " files loaded";
  std::snprintf(state.path_input, sizeof(state.path_input), "%s", summary.c_str());
}

// ---- ファイルリストマージ --------------------------------------------

// input_paths をファイルリストへ反映します。
// append=false のとき: 既存リストをクリアして新規読み込み
// append=true のとき : 既存リストへ追加（重複は除外）
static void merge_paths(AppState& state, const std::vector<fs::path>& input_paths, bool append) {
  if (input_paths.empty()) {
    state.last_save_succeeded = false;
    state.status = "Path does not exist.";
    return;
  }

  bool included_directory = false;
  fs::path first_valid_input;
  const std::vector<fs::path> collected = collect_supported_paths(input_paths, included_directory, first_valid_input);
  if (collected.empty()) {
    state.last_save_succeeded = false;
    state.status = append ? "No new supported images were found." : "No supported images were found.";
    return;
  }

  int first_new_index = 0;
  if (!append) {
    // 新規読み込み: リストと選択状態を初期化してから上書きします。
    state.files.clear();
    state.selected_index = -1;
    state.library_page = 0;
    clear_current_image(state);
    state.files = collected;
  } else {
    // 追加: 既存要素を set に変換して重複を効率よく検出します。
    std::set<fs::path> existing(state.files.begin(), state.files.end());
    first_new_index = static_cast<int>(state.files.size());
    int added_count = 0;
    for (const auto& item : collected) {
      if (existing.insert(item).second) {
        state.files.push_back(item);
        ++added_count;
      }
    }

    if (added_count == 0) {
      state.last_save_succeeded = false;
      state.status = "All selected files are already loaded.";
      update_source_summary(state);
      return;
    }
  }

  // current_dir を先頭ファイルの親ディレクトリとして更新します。
  state.current_dir = state.files.front().has_parent_path() ? state.files.front().parent_path() : fs::current_path();
  // 追加かつ出力先が設定済みの場合は上書きしません。
  if (!append || parse_user_path_input(state.output_input).empty()) {
    set_output_input(state, default_output_dir(state));
  }
  update_source_summary(state);
  state.last_save_succeeded = false;

  // 選択状態がない場合、または無効インデックスの場合は先頭（または追加の先頭）を自動選択します。
  if (!append || state.selected_index < 0 || state.selected_index >= static_cast<int>(state.files.size())) {
    load_selected_image(state, append ? first_new_index : 0);
  }

  if (append) {
    state.status = "Added files. Total: " + std::to_string(state.files.size());
  } else {
    state.status = "Loaded " + std::to_string(state.files.size()) + " image(s). Selected: " + display_name(state.files.front());
  }
}

// ---- 公開ファイル読み込み関数 ----------------------------------------

void load_path(AppState& state, const fs::path& input_path) {
  load_paths(state, std::vector<fs::path>{input_path});
}

void load_paths(AppState& state, const std::vector<fs::path>& input_paths) {
  merge_paths(state, input_paths, false);
}

void append_paths(AppState& state, const std::vector<fs::path>& input_paths) {
  merge_paths(state, input_paths, true);
}

// ---- 画像選択・読み込み ----------------------------------------------

void load_selected_image(AppState& state, int index) {
  if (index < 0 || index >= static_cast<int>(state.files.size())) {
    return;
  }

  // 前の画像データをクリアしてからデコードします。
  clear_current_image(state);

  Image loaded = {0, 0, 0, nullptr};
  const std::string file_str = state.files[static_cast<size_t>(index)].string();
  const int rc = img_load_rgba(file_str.c_str(), &loaded);
  if (rc != 0) {
    state.last_save_succeeded = false;
    if (rc == -10) {
      // PDF 読み込みは macOS 以外で未対応です。
      state.status = "PDF loading is not supported on this platform yet.";
    } else {
      state.status = "Failed to load image: " + file_str;
    }
    return;
  }

  // デコード済み画像を OpenGL テクスチャへアップロードしてプレビューを更新します。
  if (!upload_to_texture(state.preview_texture, loaded)) {
    img_free(&loaded);
    state.last_save_succeeded = false;
    state.status = "Failed to upload OpenGL texture.";
    return;
  }

  state.current_image = loaded;
  state.selected_index = index;
  state.library_page = index / 6;  // ライブラリパネルの表示ページを自動移動
  state.new_width = loaded.w;       // リサイズ入力欄を元画像サイズで初期化
  state.new_height = loaded.h;
  state.last_save_succeeded = false;
  state.status = "Loaded: " + display_name(state.files[static_cast<size_t>(index)]);
}

// ---- 出力パス生成 ----------------------------------------------------

// リサイズ後のファイル名を生成します。
// 形式: "元のファイル名_幅x高さ.拡張子"（例: photo_1920x1080.jpg）
static fs::path make_output_path(const fs::path& base_dir, const fs::path& src_path, int w, int h, int output_format) {
  const std::string stem = src_path.stem().string();
  const std::string filename =
      stem + "_" + std::to_string(w) + "x" + std::to_string(h) + output_format_extension(output_format);
  return base_dir / filename;
}

// ---- リサイズ保存 ----------------------------------------------------

bool resize_selected_and_save(AppState& state) {
  // 画像未選択チェック
  if (!state.current_image.pixels || state.selected_index < 0 || state.selected_index >= static_cast<int>(state.files.size())) {
    state.last_save_succeeded = false;
    state.status = "No image selected.";
    return false;
  }

  // サイズ入力値の検証
  if (state.new_width <= 0 || state.new_height <= 0) {
    state.last_save_succeeded = false;
    state.status = "Width and Height must be positive.";
    return false;
  }

  // C コアでリサイズを実行します。
  Image resized = {0, 0, 0, nullptr};
  const int rc_resize = img_resize_rgba(&state.current_image, state.new_width, state.new_height, &resized);
  if (rc_resize != 0) {
    state.last_save_succeeded = false;
    state.status = "Resize failed.";
    return false;
  }

  // 出力ディレクトリが存在しない場合は作成します。
  const fs::path src = state.files[static_cast<size_t>(state.selected_index)];
  const fs::path output_dir = get_output_dir(state);
  if (!ensure_directory_exists(output_dir, state.status)) {
    img_free(&resized);
    state.last_save_succeeded = false;
    return false;
  }

  const fs::path out = make_output_path(output_dir, src, state.new_width, state.new_height, state.output_format);
  const std::string out_str = out.string();

  // 指定フォーマットで保存し、リサイズ後バッファを解放します。
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

void resize_all_and_save(AppState& state) {
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

  // ファイルリスト全体を順番に処理します。
  // 各ファイルは独立して読み込み・リサイズ・保存を行い、都度メモリを解放します。
  for (const auto& src : state.files) {
    Image loaded = {0, 0, 0, nullptr};
    const std::string src_str = src.string();

    if (img_load_rgba(src_str.c_str(), &loaded) != 0) {
      ++fail_count;
      continue;
    }

    Image resized = {0, 0, 0, nullptr};
    const int rc_resize = img_resize_rgba(&loaded, state.new_width, state.new_height, &resized);
    img_free(&loaded);  // 元画像は不要になったので直ちに解放します。

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

  // すべて成功した場合のみ last_save_succeeded を true にします。
  state.last_save_succeeded = (success_count > 0 && fail_count == 0);
  state.status = "Batch resize finished. success=" + std::to_string(success_count) + ", fail=" + std::to_string(fail_count);
}
