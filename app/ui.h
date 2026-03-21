#pragma once

#include "app_state.h"

struct ImGuiIO;

void configure_ui_fonts(ImGuiIO& io);
void apply_ui_theme();
void draw_ui(AppState& state);
