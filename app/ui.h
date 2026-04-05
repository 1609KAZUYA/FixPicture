// UI レイヤーの公開インターフェースです。
// ImGui フォント設定・テーマ適用・フレーム描画の 3 つの関数を提供します。
// ImGuiIO や ImGui の詳細ヘッダはこのファイルに含めず、ui.cpp 内に閉じ込めます。
#pragma once

#include "app_state.h"

// ImGuiIO の前方宣言です。
// このヘッダから <imgui.h> を除外してインクルード依存を最小化します。
struct ImGuiIO;

// フォントを設定します。
// プラットフォームごとに候補リストからシステムフォントを探してロードし、
// 日本語グリフを別フォントからマージします。
// Application::init_imgui() から ImGui コンテキスト生成直後に一度だけ呼びます。
void configure_ui_fonts(ImGuiIO& io);

// ImGui のスタイル（角丸・余白・カラーパレット）を設定します。
// パープル/シアン系のカラーテーマを適用します。
// configure_ui_fonts() の後、バックエンド初期化の前に呼びます。
void apply_ui_theme();

// 1フレーム分の UI 全体を描画します。
// 背景グラデーション・ライブラリパネル・コントロールパネル・プレビューパネルを
// ImGui ウィジェットで構築します。メインループから毎フレーム呼ばれます。
void draw_ui(AppState& state);
