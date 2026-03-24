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

constexpr ImVec4 kBgA = ImVec4(0.08f, 0.05f, 0.18f, 1.00f);
constexpr ImVec4 kBgB = ImVec4(0.04f, 0.12f, 0.18f, 1.00f);
constexpr ImVec4 kSidebar = ImVec4(0.10f, 0.11f, 0.20f, 0.98f);
constexpr ImVec4 kPanel = ImVec4(0.11f, 0.15f, 0.24f, 0.98f);
constexpr ImVec4 kBorder = ImVec4(0.55f, 0.49f, 0.98f, 0.34f);
constexpr ImVec4 kAccentCyan = ImVec4(0.48f, 0.95f, 1.00f, 1.00f);
constexpr ImVec4 kAccentPink = ImVec4(1.00f, 0.48f, 0.82f, 1.00f);
constexpr ImVec4 kButton = ImVec4(0.46f, 0.30f, 0.98f, 1.00f);
constexpr ImVec4 kButtonHover = ImVec4(0.58f, 0.37f, 1.00f, 1.00f);
constexpr ImVec4 kButtonActive = ImVec4(0.26f, 0.58f, 0.96f, 1.00f);
constexpr ImVec4 kTextStrong = ImVec4(0.98f, 0.99f, 1.00f, 1.00f);
constexpr ImVec4 kTextBody = ImVec4(0.91f, 0.94f, 1.00f, 1.00f);
constexpr ImVec4 kTextMuted = ImVec4(0.75f, 0.81f, 0.92f, 1.00f);
constexpr float kMinLibraryWidth = 280.0f;
constexpr float kMaxLibraryWidth = 340.0f;
constexpr float kMinControlWidth = 360.0f;
constexpr float kMaxControlWidth = 420.0f;

ImFont* g_font_regular = nullptr;
ImFont* g_font_bold = nullptr;
ImFont* g_font_japanese = nullptr;

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

template <size_t N>
bool try_merge_font(
    ImGuiIO& io,
    const std::array<const char*, N>& candidates,
    float size_pixels,
    const ImWchar* glyph_ranges) {
  for (const char* path : candidates) {
    if (!path) {
      continue;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
      continue;
    }

    ImFontConfig config;
    config.MergeMode = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    if (io.Fonts->AddFontFromFileTTF(path, size_pixels, &config, glyph_ranges)) {
      return true;
    }
  }

  return false;
}

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

ImFont* pick_filename_font(const std::string& text) {
  (void)text;
  if (g_font_regular) {
    return g_font_regular;
  }
  return ImGui::GetFont();
}

void draw_background() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
  const ImVec2 p0 = viewport->Pos;
  const ImVec2 p1 = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);

  draw_list->AddRectFilledMultiColor(
      p0,
      p1,
      ImGui::ColorConvertFloat4ToU32(kBgA),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.11f, 0.19f, 1.00f)),
      ImGui::ColorConvertFloat4ToU32(kBgB),
      ImGui::ColorConvertFloat4ToU32(kBgB));

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

void begin_surface(const char* id, const ImVec2& size, const ImVec4& color) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 24.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color);
  ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
  ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void end_surface() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void draw_eyebrow(const char* text) {
  push_bold_font();
  ImGui::TextColored(kAccentCyan, "%s", text);
  pop_bold_font();
}

void draw_title(const char* text) {
  push_bold_font();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextStrong);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  pop_bold_font();
}

void draw_caption(const char* text) {
  push_regular_font();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(kTextMuted, "%s", text);
  ImGui::PopTextWrapPos();
  pop_regular_font();
}

void draw_divider() {
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.44f, 0.40f, 0.74f, 0.88f));
  ImGui::Separator();
  ImGui::PopStyleColor();
}

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

void draw_export_controls(AppState& state) {
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

void draw_status_block(const AppState& state) {
  push_bold_font();
  ImGui::TextColored(state.last_save_succeeded ? ImVec4(0.68f, 1.00f, 0.78f, 1.00f) : kAccentPink,
                     state.last_save_succeeded ? "Saved" : "Status");
  pop_bold_font();
  ImGui::TextWrapped("%s", state.status.c_str());
}

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

  const float reserved_height = 58.0f;
  const float row_height = 42.0f + ImGui::GetStyle().ItemSpacing.y;
  const float usable_height = std::max(42.0f, ImGui::GetContentRegionAvail().y - reserved_height);
  const int rows_per_page = std::max(1, static_cast<int>(usable_height / row_height));
  const int page_count = std::max(1, (static_cast<int>(state.files.size()) + rows_per_page - 1) / rows_per_page);

  if (state.selected_index >= 0) {
    state.library_page = std::clamp(state.selected_index / rows_per_page, 0, page_count - 1);
  } else {
    state.library_page = std::clamp(state.library_page, 0, page_count - 1);
  }

  const int start = state.library_page * rows_per_page;
  const int end = std::min(start + rows_per_page, static_cast<int>(state.files.size()));

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
    const bool clicked = ImGui::Selectable("##file_row", selected, 0, row_size);
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
      load_selected_image(state, i);
    }
  }

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

  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(cursor, ImVec2(cursor.x + avail.x, cursor.y + avail.y), IM_COL32(16, 18, 30, 240), 20.0f);
  draw_list->AddRect(cursor, ImVec2(cursor.x + avail.x, cursor.y + avail.y), IM_COL32(142, 124, 255, 96), 20.0f, 0, 1.2f);

  if (state.preview_texture.id == 0) {
    ImGui::SetCursorScreenPos(ImVec2(cursor.x + 28.0f, cursor.y + 28.0f));
    draw_title("No preview yet");
    ImGui::TextColored(kTextBody, "Open a file or drop it here.");
    ImGui::TextColored(kAccentCyan, "PNG, JPG, PDF");
    ImGui::Dummy(avail);
    end_surface();
    return;
  }

  const float max_w = std::max(1.0f, avail.x - 36.0f);
  const float max_h = std::max(1.0f, avail.y - 36.0f);
  const float scale_w = max_w / static_cast<float>(state.preview_texture.width);
  const float scale_h = max_h / static_cast<float>(state.preview_texture.height);
  const float scale = std::min(1.0f, std::min(scale_w, scale_h));

  const ImVec2 size(
      state.preview_texture.width * scale,
      state.preview_texture.height * scale);
  const ImVec2 image_pos(
      cursor.x + (avail.x - size.x) * 0.5f,
      cursor.y + (avail.y - size.y) * 0.5f);

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

void configure_ui_fonts(ImGuiIO& io) {
  io.Fonts->Clear();

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

  if (!g_font_regular) {
    ImFontConfig base_config;
    base_config.SizePixels = 20.0f;
    base_config.OversampleH = 1;
    base_config.OversampleV = 1;
    base_config.PixelSnapH = true;
    g_font_regular = io.Fonts->AddFontDefault(&base_config);
  }
  if (!g_font_bold) {
    g_font_bold = g_font_regular;
  }

  io.FontDefault = g_font_regular;

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
  try_merge_font(io, japanese_candidates, 20.0f, japanese_ranges);
  g_font_japanese = g_font_regular;
}

void apply_ui_theme() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 24.0f;
  style.FrameRounding = 16.0f;
  style.PopupRounding = 18.0f;
  style.ScrollbarRounding = 999.0f;
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

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = kTextStrong;
  colors[ImGuiCol_TextDisabled] = kTextMuted;
  colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
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
  draw_background();

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings;

  ImGui::Begin("ImageToolShell", nullptr, flags);
  const float library_width = std::clamp(viewport->Size.x * 0.24f, kMinLibraryWidth, kMaxLibraryWidth);
  const float control_width = std::clamp(viewport->Size.x * 0.30f, kMinControlWidth, kMaxControlWidth);

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
