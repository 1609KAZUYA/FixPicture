// UI 描画の実装です。
// ImGui を使って背景・ライブラリパネル・コントロールパネル・プレビューパネルを描画します。
// このファイル内の関数はすべて匿名 namespace に閉じ込め、
// 外部へ公開する関数は末尾の configure_ui_fonts / apply_ui_theme / draw_ui のみです。
#include "ui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "file_dialogs.h"
#include "image_session.h"

namespace {

// ---- カラー定数 -------------------------------------------------------
// UI 全体で使うパープル/シアン系のカラーパレットです。
// 変更する場合はここを修正するだけで全パネルに反映されます。
constexpr ImVec4 kBgA = ImVec4(0.08f, 0.05f, 0.18f, 1.00f);       // 背景グラデーション左上
constexpr ImVec4 kBgB = ImVec4(0.04f, 0.12f, 0.18f, 1.00f);       // 背景グラデーション右下
constexpr ImVec4 kSidebar = ImVec4(0.10f, 0.11f, 0.20f, 0.98f);   // サイドバー背景
constexpr ImVec4 kPanel = ImVec4(0.11f, 0.15f, 0.24f, 0.98f);     // プレビューパネル背景
constexpr ImVec4 kBorder = ImVec4(0.55f, 0.49f, 0.98f, 0.34f);    // パネル枠線
constexpr ImVec4 kAccentCyan = ImVec4(0.48f, 0.95f, 1.00f, 1.00f); // シアンアクセント
constexpr ImVec4 kAccentPink = ImVec4(1.00f, 0.48f, 0.82f, 1.00f); // ピンクアクセント
constexpr ImVec4 kButton = ImVec4(0.46f, 0.30f, 0.98f, 1.00f);    // ボタン通常色
constexpr ImVec4 kButtonHover = ImVec4(0.58f, 0.37f, 1.00f, 1.00f); // ボタンホバー色
constexpr ImVec4 kButtonActive = ImVec4(0.26f, 0.58f, 0.96f, 1.00f); // ボタン押下色
constexpr ImVec4 kTextStrong = ImVec4(0.98f, 0.99f, 1.00f, 1.00f); // 強調テキスト（白に近い）
constexpr ImVec4 kTextBody = ImVec4(0.91f, 0.94f, 1.00f, 1.00f);  // 本文テキスト
constexpr ImVec4 kTextMuted = ImVec4(0.75f, 0.81f, 0.92f, 1.00f); // 補足テキスト（グレー系）

// ---- レイアウト定数 ---------------------------------------------------
// ライブラリパネルの最小・最大幅です。画面幅に応じてこの範囲でクランプされます。
constexpr float kMinLibraryWidth = 280.0f;
constexpr float kMaxLibraryWidth = 340.0f;
// コントロールパネルの最小・最大幅です。
constexpr float kMinControlWidth = 360.0f;
constexpr float kMaxControlWidth = 420.0f;

// ---- フォントキャッシュ -----------------------------------------------
// configure_ui_fonts() でロードしたフォントへのポインタを保持します。
// ImGui が管理するポインタのため、自分で解放しません。
ImFont* g_font_regular = nullptr;  // 通常フォント（本文・ラベル用）
ImFont* g_font_bold = nullptr;     // 太字フォント（見出し用）
ImFont* g_font_japanese = nullptr; // 日本語フォント（g_font_regular にマージ済み）

// ---- フォント読み込みヘルパー ----------------------------------------

// 候補リストから最初に見つかったフォントファイルを ImGui へロードします。
// ロードに成功した場合は out_font にポインタを格納して true を返します。
template <size_t N>
bool try_load_base_font(
    ImGuiIO& io,
    const std::array<const char*, N>& candidates,
    float size_pixels,
    ImFont*& out_font) {
  ImFontConfig config;
  config.OversampleH = 1;
  config.OversampleV = 1;
  config.PixelSnapH = true;

  for (const char* path : candidates) {
    if (!path) {
      continue;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
      continue;
    }

    ImFont* font = io.Fonts->AddFontFromFileTTF(path, size_pixels, &config);
    if (font) {
      out_font = font;
      return true;
    }
  }

  return false;
}

// 既存フォントへ日本語グリフを追加マージします。
// MergeMode を使うことで、英語フォントと日本語フォントを一つのフォントとして扱えます。
// destination_font が nullptr の場合は何もしません。
template <size_t N>
bool try_merge_font(
    ImGuiIO& io,
    const std::array<const char*, N>& candidates,
    float size_pixels,
    const ImWchar* glyph_ranges,
    ImFont* destination_font) {
  if (!destination_font) {
    return false;
  }

  for (const char* path : candidates) {
    if (!path) {
      continue;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
      continue;
    }

    ImFontConfig config;
    config.MergeMode = true;       // 既存フォントへ追加するモード
    config.DstFont = destination_font;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    if (io.Fonts->AddFontFromFileTTF(path, size_pixels, &config, glyph_ranges)) {
      return true;
    }
  }

  return false;
}

// ---- フォント Push/Pop ヘルパー ---------------------------------------
// ImGui::PushFont / PopFont のラッパーです。
// フォントが nullptr の場合は ImGui のデフォルトフォントを継続使用します。

void push_regular_font() {
  if (g_font_regular) {
    ImGui::PushFont(g_font_regular);
  }
}

void pop_regular_font() {
  if (g_font_regular) {
    ImGui::PopFont();
  }
}

void push_bold_font() {
  if (g_font_bold) {
    ImGui::PushFont(g_font_bold);
  }
}

void pop_bold_font() {
  if (g_font_bold) {
    ImGui::PopFont();
  }
}

// ファイル名の表示に使うフォントを選択します。
// 現在は常に g_font_regular を返しますが、将来 CJK 判定などを追加できます。
ImFont* pick_filename_font(const std::string& text) {
  (void)text;
  if (g_font_regular) {
    return g_font_regular;
  }
  return ImGui::GetFont();
}

// ---- 背景描画 ---------------------------------------------------------

// ウィンドウ全体に渡るグラデーション背景と装飾用の半透明円を描画します。
// ImGui のバックグラウンド描画リストを使うため、ウィジェットより必ず後ろに描かれます。
void draw_background() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
  const ImVec2 p0 = viewport->Pos;
  const ImVec2 p1 = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);

  // 4 隅に異なる色を指定することで対角方向のグラデーションを作ります。
  draw_list->AddRectFilledMultiColor(
      p0,
      p1,
      ImGui::ColorConvertFloat4ToU32(kBgA),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.11f, 0.19f, 1.00f)),
      ImGui::ColorConvertFloat4ToU32(kBgB),
      ImGui::ColorConvertFloat4ToU32(kBgB));

  // 装飾用の半透明円を 3 か所配置します（左上・右上・右下）。
  draw_list->AddCircleFilled(
      ImVec2(p0.x + viewport->Size.x * 0.14f, p0.y + viewport->Size.y * 0.12f),
      viewport->Size.x * 0.10f,
      IM_COL32(255, 96, 214, 28),
      72);
  draw_list->AddCircleFilled(
      ImVec2(p0.x + viewport->Size.x * 0.84f, p0.y + viewport->Size.y * 0.16f),
      viewport->Size.x * 0.10f,
      IM_COL32(84, 236, 255, 24),
      72);
  draw_list->AddCircleFilled(
      ImVec2(p0.x + viewport->Size.x * 0.74f, p0.y + viewport->Size.y * 0.82f),
      viewport->Size.x * 0.13f,
      IM_COL32(123, 103, 255, 22),
      72);
}

// ---- パネルコンテナ ---------------------------------------------------

// 角丸・ボーダー付きの子ウィンドウ（パネル）を開始します。
// end_surface() と必ずペアで使います。
void begin_surface(const char* id, const ImVec2& size, const ImVec4& color) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 24.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color);
  ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
  ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// begin_surface() で Push したスタイルを解放し、子ウィンドウを閉じます。
void end_surface() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

// ---- テキスト描画ヘルパー --------------------------------------------

// シアン色の小見出しラベル（例: "LIBRARY"）を描画します。
void draw_eyebrow(const char* text) {
  push_bold_font();
  ImGui::TextColored(kAccentCyan, "%s", text);
  pop_bold_font();
}

// 太字の白色タイトルテキストを描画します。
void draw_title(const char* text) {
  push_bold_font();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextStrong);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  pop_bold_font();
}

// グレー系の補足テキストを描画します。
void draw_caption(const char* text) {
  push_regular_font();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kTextMuted, "%s", text);
  ImGui::PopTextWrapPos();
  pop_regular_font();
}

// パネル内の水平区切り線を描画します。
void draw_divider() {
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.44f, 0.40f, 0.74f, 0.88f));
  ImGui::Separator();
  ImGui::PopStyleColor();
}

// ---- コントロールパネル内ウィジェット --------------------------------

// 「Open File」「Open Folder」ボタンを横並びで描画します。
// ボタン押下時はネイティブダイアログを開き、選択パスを AppState へ追加します。
void draw_open_buttons(AppState& state) {
  const float button_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

  if (ImGui::Button("Open File", ImVec2(button_width, 38.0f))) {
    std::vector<fs::path> selected_paths;
    if (prompt_open_image_files(selected_paths)) {
      append_paths(state, selected_paths);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Open Folder", ImVec2(button_width, 38.0f))) {
    fs::path selected_path;
    if (prompt_select_image_directory(selected_path)) {
      append_paths(state, std::vector<fs::path>{selected_path});
    }
  }
}

// 現在のソースパスを読み取り専用テキストボックスに表示します。
// ファイルが複数の場合は "N files loaded" 形式の要約文字列になります。
void draw_path_row(AppState& state) {
  push_bold_font();
  ImGui::TextColored(kTextBody, "Source");
  pop_bold_font();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint(
      "##path",
      "Use Open File, Open Folder, or drag and drop",
      state.path_input,
      IM_ARRAYSIZE(state.path_input),
      ImGuiInputTextFlags_ReadOnly);
}

// 出力フォルダの入力欄と「Choose」ボタンを描画します。
// 「Choose」押下でネイティブダイアログからフォルダを選択できます。
void draw_output_row(AppState& state) {
  const float action_width = 110.0f;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - action_width - ImGui::GetStyle().ItemSpacing.x);
  ImGui::InputTextWithHint("##output", "Save folder", state.output_input, IM_ARRAYSIZE(state.output_input));
  ImGui::SameLine();
  if (ImGui::Button("Choose", ImVec2(action_width, 0.0f))) {
    fs::path selected_path;
    if (prompt_select_output_directory(selected_path)) {
      const std::string text = selected_path.string();
      std::snprintf(state.output_input, sizeof(state.output_input), "%s", text.c_str());
    }
  }
}

// 出力フォーマット（PNG/JPG/PDF）・品質スライダー・Width/Height 入力欄を描画します。
// JPG/PDF 選択時のみ品質スライダーが有効になり、PNG の場合は「Lossless」と表示します。
void draw_export_controls(AppState& state) {
  // フォーマットと品質を 2 列テーブルで横並びにします。
  if (ImGui::BeginTable("control_format_row", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadInnerX)) {
    ImGui::TableNextColumn();
    push_bold_font();
    ImGui::TextColored(kTextBody, "Format");
    pop_bold_font();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##format", output_format_label(state.output_format))) {
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

    ImGui::TableNextColumn();
    push_bold_font();
    ImGui::TextColored(kTextBody, "Quality");
    pop_bold_font();
    if (state.output_format == IMG_FORMAT_JPG || state.output_format == IMG_FORMAT_PDF) {
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::SliderInt("##quality", &state.jpg_quality, 1, 100);
    } else {
      ImGui::TextColored(kTextMuted, "Lossless");
    }
    ImGui::EndTable();
  }

  // Width と Height を手動で横並びにします。
  // BeginTable を使わず SetCursorPos でオフセットすることでより細かく位置を制御します。
  const float group_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  const ImVec2 start_pos = ImGui::GetCursorPos();

  ImGui::BeginGroup();
  push_bold_font();
  ImGui::TextColored(kTextBody, "Width");
  pop_bold_font();
  ImGui::SetNextItemWidth(group_width);
  ImGui::InputInt("##width", &state.new_width);
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(start_pos.x + group_width + ImGui::GetStyle().ItemSpacing.x, start_pos.y));

  ImGui::BeginGroup();
  push_bold_font();
  ImGui::TextColored(kTextBody, "Height");
  pop_bold_font();
  ImGui::SetNextItemWidth(group_width);
  ImGui::InputInt("##height", &state.new_height);
  ImGui::EndGroup();
}

// 「Resize Selected」「Resize All」ボタンを横並びで描画します。
// それぞれ image_session の resize 関数を呼び出します。
void draw_resize_buttons(AppState& state) {
  const float button_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

  if (ImGui::Button("Resize Selected", ImVec2(button_width, 40.0f))) {
    resize_selected_and_save(state);
  }
  ImGui::SameLine();
  if (ImGui::Button("Resize All", ImVec2(button_width, 40.0f))) {
    resize_all_and_save(state);
  }
}

// 最後の操作結果を表示するステータスブロックを描画します。
// 成功時は緑色の「Saved」、失敗/未保存時はピンク色の「Status」ラベルになります。
void draw_status_block(const AppState& state) {
  push_bold_font();
  ImGui::TextColored(state.last_save_succeeded ? ImVec4(0.68f, 1.00f, 0.78f, 1.00f) : kAccentPink,
                     state.last_save_succeeded ? "Saved" : "Status");
  pop_bold_font();
  ImGui::TextWrapped("%s", state.status.c_str());
}

// ---- 3 つのメインパネル ----------------------------------------------

// コントロールパネルを描画します（中央列）。
// ファイルオープン・出力設定・フォーマット・サイズ・保存ボタン・ステータスを含みます。
void draw_control_panel(AppState& state, float height) {
  begin_surface("control_panel", ImVec2(0.0f, height), kSidebar);
  draw_title("Open, resize, save");
  draw_caption("Open a file, set output, then resize.");
  draw_divider();
  draw_open_buttons(state);
  draw_path_row(state);
  draw_output_row(state);
  draw_divider();
  draw_export_controls(state);
  draw_resize_buttons(state);
  draw_divider();
  draw_status_block(state);
  end_surface();
}

// ライブラリパネルを描画します（左列）。
// ロード済みファイルの一覧をページネーション付きで表示し、
// 行クリックで対象画像を読み込みプレビューを更新します。
void draw_library_panel(AppState& state, float height) {
  begin_surface("library_panel", ImVec2(0.0f, height), kSidebar);
  draw_eyebrow("LIBRARY");
  draw_title("Loaded files");
  draw_caption(state.files.empty() ? "No files yet." : "Tap a file to preview it.");
  draw_divider();

  if (state.files.empty()) {
    ImGui::TextColored(kTextMuted, "Drop a file or choose a folder.");
    end_surface();
    return;
  }

  // 表示できる行数を計算し、ページを決定します。
  const float reserved_height = 58.0f;  // ページネーションボタン分の予約高さ
  const float row_height = 42.0f + ImGui::GetStyle().ItemSpacing.y;
  const float usable_height = std::max(42.0f, ImGui::GetContentRegionAvail().y - reserved_height);
  const int rows_per_page = std::max(1, static_cast<int>(usable_height / row_height));
  const int page_count = std::max(1, (static_cast<int>(state.files.size()) + rows_per_page - 1) / rows_per_page);

  // 選択中のインデックスが含まれるページへ自動スクロールします。
  if (state.selected_index >= 0) {
    state.library_page = std::clamp(state.selected_index / rows_per_page, 0, page_count - 1);
  } else {
    state.library_page = std::clamp(state.library_page, 0, page_count - 1);
  }

  const int start = state.library_page * rows_per_page;
  const int end = std::min(start + rows_per_page, static_cast<int>(state.files.size()));

  // 現在のページに含まれるファイル行を描画します。
  for (int i = start; i < end; ++i) {
    const bool selected = (i == state.selected_index);
    std::string name = display_name(state.files[static_cast<size_t>(i)]);
    if (name.empty()) {
      name = state.files[static_cast<size_t>(i)].string();
    }
    ImGui::PushID(i);
    ImFont* file_font = pick_filename_font(name);
    if (file_font) {
      ImGui::PushFont(file_font);
    }
    const ImVec2 row_size(ImGui::GetContentRegionAvail().x, 42.0f);
    // 当たり判定用の透明 Selectable を配置し、クリックを検出します。
    const bool clicked = ImGui::Selectable("##file_row", selected, 0, row_size);
    // テキストは DrawList で直接描画することで、Selectable の選択ハイライトと重ねられます。
    const ImVec2 row_min = ImGui::GetItemRectMin();
    const ImVec2 row_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 text_color = ImGui::ColorConvertFloat4ToU32(selected ? kTextStrong : kTextBody);
    draw_list->PushClipRect(row_min, row_max, true);
    draw_list->AddText(
        file_font ? file_font : ImGui::GetFont(),
        file_font ? file_font->FontSize : ImGui::GetFontSize(),
        ImVec2(row_min.x + 14.0f, row_min.y + 10.0f),
        text_color,
        name.c_str());
    draw_list->PopClipRect();
    if (file_font) {
      ImGui::PopFont();
    }
    ImGui::PopID();
    if (clicked) {
      // クリックされた行の画像を読み込み、プレビューとサイズ入力欄を更新します。
      load_selected_image(state, i);
    }
  }

  // ページネーションボタンを描画します。
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
  if (ImGui::Button("Prev", ImVec2(92.0f, 36.0f)) && state.library_page > 0) {
    --state.library_page;
  }
  ImGui::SameLine();
  ImGui::TextColored(kTextBody, "%d / %d", state.library_page + 1, page_count);
  ImGui::SameLine();
  if (ImGui::Button("Next", ImVec2(92.0f, 36.0f)) && state.library_page < page_count - 1) {
    ++state.library_page;
  }
  end_surface();
}

// プレビューパネルを描画します（右列）。
// 選択中の画像を OpenGL テクスチャ経由でアスペクト比を維持しながら中央に表示します。
void draw_preview_panel(const AppState& state, float height) {
  begin_surface("preview_panel", ImVec2(0.0f, height), kPanel);
  draw_eyebrow("PREVIEW");
  draw_title("Current image");
  if (state.selected_index >= 0 && state.selected_index < static_cast<int>(state.files.size())) {
    const std::string name = display_name(state.files[static_cast<size_t>(state.selected_index)]);
    ImFont* file_font = pick_filename_font(name);
    if (file_font) {
      ImGui::PushFont(file_font);
    }
    draw_caption(name.c_str());
    if (file_font) {
      ImGui::PopFont();
    }
  } else {
    draw_caption("The selected file appears here.");
  }
  draw_divider();

  // 画像表示エリアの背景（暗い角丸矩形）と枠線を描画します。
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(cursor, ImVec2(cursor.x + avail.x, cursor.y + avail.y), IM_COL32(16, 18, 30, 240), 20.0f);
  draw_list->AddRect(cursor, ImVec2(cursor.x + avail.x, cursor.y + avail.y), IM_COL32(142, 124, 255, 96), 20.0f, 0, 1.2f);

  // テクスチャが未生成（画像未選択）の場合はプレースホルダーを表示して終了します。
  if (state.preview_texture.id == 0) {
    ImGui::SetCursorScreenPos(ImVec2(cursor.x + 28.0f, cursor.y + 28.0f));
    draw_title("No preview yet");
    ImGui::TextColored(kTextBody, "Open a file or drop it here.");
    ImGui::TextColored(kAccentCyan, "PNG, JPG, PDF");
    ImGui::Dummy(avail);
    end_surface();
    return;
  }

  // テクスチャのアスペクト比を維持しながら、利用可能領域に収まる最大スケールを計算します。
  const float max_w = std::max(1.0f, avail.x - 36.0f);
  const float max_h = std::max(1.0f, avail.y - 36.0f);
  const float scale_w = max_w / static_cast<float>(state.preview_texture.width);
  const float scale_h = max_h / static_cast<float>(state.preview_texture.height);
  const float scale = std::min(1.0f, std::min(scale_w, scale_h));  // 1.0 以下に抑えて拡大しない

  const ImVec2 size(
      state.preview_texture.width * scale,
      state.preview_texture.height * scale);
  // 利用可能領域内で中央揃えになる位置を計算します。
  const ImVec2 image_pos(
      cursor.x + (avail.x - size.x) * 0.5f,
      cursor.y + (avail.y - size.y) * 0.5f);

  // 画像の後ろに影（暗い角丸矩形）を描画して浮き上がり感を出します。
  draw_list->AddRectFilled(
      ImVec2(image_pos.x - 12.0f, image_pos.y - 12.0f),
      ImVec2(image_pos.x + size.x + 12.0f, image_pos.y + size.y + 12.0f),
      IM_COL32(9, 10, 18, 255),
      16.0f);

  ImGui::SetCursorScreenPos(image_pos);
  ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(state.preview_texture.id)), size);
  ImGui::Dummy(avail);
  end_surface();
}

}  // namespace

// ---- 公開関数の実装 --------------------------------------------------

void configure_ui_fonts(ImGuiIO& io) {
  io.Fonts->Clear();

  // プラットフォームごとに候補フォントのパスリストを定義します。
  // 先頭から順に試し、最初に見つかったものを使います。
  const std::array<const char*, 6> regular_candidates = {
#if defined(__APPLE__)
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Avenir Next.ttc",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
#elif defined(_WIN32)
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
#else
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
#endif
      nullptr,
      nullptr};

  const std::array<const char*, 6> bold_candidates = {
#if defined(__APPLE__)
      "/System/Library/Fonts/SFNSRounded.ttf",
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Avenir Next.ttc",
      "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
#elif defined(_WIN32)
      "C:/Windows/Fonts/seguisb.ttf",
      "C:/Windows/Fonts/arialbd.ttf",
#else
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
#endif
      nullptr,
      nullptr};

  g_font_regular = nullptr;
  g_font_bold = nullptr;
  try_load_base_font(io, regular_candidates, 20.0f, g_font_regular);
  try_load_base_font(io, bold_candidates, 22.0f, g_font_bold);

  // システムフォントが一つも見つからない場合は ImGui 内蔵フォントを使います。
  if (!g_font_regular) {
    ImFontConfig base_config;
    base_config.SizePixels = 20.0f;
    base_config.OversampleH = 1;
    base_config.OversampleV = 1;
    base_config.PixelSnapH = true;
    g_font_regular = io.Fonts->AddFontDefault(&base_config);
  }
  // 太字フォントが見つからない場合は通常フォントで代用します。
  if (!g_font_bold) {
    g_font_bold = g_font_regular;
  }

  io.FontDefault = g_font_regular;

  // 日本語グリフを通常フォント・太字フォントの両方にマージします。
  const ImWchar* japanese_ranges = io.Fonts->GetGlyphRangesJapanese();
  const std::array<const char*, 6> japanese_candidates = {
#if defined(__APPLE__)
      "/System/Library/Fonts/Supplemental/NotoSansGothic-Regular.ttf",
      "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
      "/System/Library/Fonts/AppleSDGothicNeo.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
#elif defined(_WIN32)
      "C:/Windows/Fonts/YuGothM.ttc",
      "C:/Windows/Fonts/msgothic.ttc",
#else
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
#endif
      nullptr,
      nullptr};
  try_merge_font(io, japanese_candidates, 20.0f, japanese_ranges, g_font_regular);
  if (g_font_bold && g_font_bold != g_font_regular) {
    try_merge_font(io, japanese_candidates, 22.0f, japanese_ranges, g_font_bold);
  }
  // g_font_regular に日本語グリフがマージされているため、同じポインタで参照します。
  g_font_japanese = g_font_regular;
}

void apply_ui_theme() {
  // ImGui のグローバルスタイルにアプリ固有の値を設定します。
  // 角丸を大きくしてモダンな見た目にし、余白を広めに取ります。
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 24.0f;
  style.FrameRounding = 16.0f;
  style.PopupRounding = 18.0f;
  style.ScrollbarRounding = 999.0f;  // 完全な円形にします
  style.GrabRounding = 999.0f;
  style.WindowPadding = ImVec2(22.0f, 22.0f);
  style.FramePadding = ImVec2(18.0f, 12.0f);
  style.ItemSpacing = ImVec2(14.0f, 12.0f);
  style.ItemInnerSpacing = ImVec2(12.0f, 10.0f);
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 0.0f;
  style.ScrollbarSize = 12.0f;
  style.IndentSpacing = 18.0f;

  // カラーパレットを設定します。定数 k* を使って一貫性を保ちます。
  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = kTextStrong;
  colors[ImGuiCol_TextDisabled] = kTextMuted;
  colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // 透明（背景は draw_background で描画）
  colors[ImGuiCol_ChildBg] = kPanel;
  colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.10f, 0.22f, 0.98f);
  colors[ImGuiCol_Border] = kBorder;
  colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.14f, 0.31f, 1.0f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.19f, 0.41f, 1.0f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.29f, 0.46f, 1.0f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_Button] = kButton;
  colors[ImGuiCol_ButtonHovered] = kButtonHover;
  colors[ImGuiCol_ButtonActive] = kButtonActive;
  colors[ImGuiCol_Header] = ImVec4(0.22f, 0.18f, 0.42f, 1.0f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.24f, 0.58f, 1.0f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.38f, 0.64f, 1.0f);
  colors[ImGuiCol_CheckMark] = kAccentCyan;
  colors[ImGuiCol_SliderGrab] = kAccentPink;
  colors[ImGuiCol_SliderGrabActive] = kAccentCyan;
  colors[ImGuiCol_Separator] = ImVec4(0.44f, 0.38f, 0.72f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.10f, 0.18f, 0.55f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.22f, 0.48f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.39f, 0.31f, 0.62f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.22f, 0.44f, 0.72f, 1.0f);
}

void draw_ui(AppState& state) {
  // 背景グラデーションと装飾円を描画します（ウィジェットの後ろ側）。
  draw_background();

  // アプリウィンドウをビューポート全体に広げ、タイトルバーや移動を無効にします。
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings;

  ImGui::Begin("ImageToolShell", nullptr, flags);

  // 画面幅に応じてパネル幅を計算し、範囲外はクランプします。
  const float library_width = std::clamp(viewport->Size.x * 0.24f, kMinLibraryWidth, kMaxLibraryWidth);
  const float control_width = std::clamp(viewport->Size.x * 0.30f, kMinControlWidth, kMaxControlWidth);

  // 3列レイアウト: 左=ライブラリ / 中=コントロール / 右=プレビュー（残り幅を伸縮）
  if (ImGui::BeginTable("main_layout", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX)) {
    ImGui::TableSetupColumn("library", ImGuiTableColumnFlags_WidthFixed, library_width);
    ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, control_width);
    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextColumn();
    draw_library_panel(state, 0.0f);

    ImGui::TableNextColumn();
    draw_control_panel(state, 0.0f);

    ImGui::TableNextColumn();
    draw_preview_panel(state, 0.0f);

    ImGui::EndTable();
  }

  ImGui::End();
}
