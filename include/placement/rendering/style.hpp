#pragma once

#include <string_view>

namespace placement::rendering_style {

struct Palette {
  std::string_view background;
  std::string_view row_fill;
  std::string_view row_stroke;
  std::string_view movable_fill;
  std::string_view macro_fill;
  std::string_view macro_stroke;
  std::string_view fixed_fill;
  std::string_view fixed_stroke;
  std::string_view fixed_non_interacting_fill;
  std::string_view fixed_non_interacting_stroke;
  std::string_view surface;
  std::string_view grid_stroke;
  std::string_view overlay_stroke;
  std::string_view non_interacting_overlay_stroke;
  std::string_view unavailable;
  unsigned heatmap_saturation_percent;
  unsigned heatmap_lightness_percent;
};

inline constexpr Palette default_palette{
    .background = "#2C2C2C",
    .row_fill = "#e2e8f0",
    .row_stroke = "#94a3b8",
    .movable_fill = "#3b82f6",
    .macro_fill = "#ffffff",
    .macro_stroke = "#1f2937",
    .fixed_fill = "#ffffff",
    .fixed_stroke = "#ffffff",
    .fixed_non_interacting_fill = "#f59e0b",
    .fixed_non_interacting_stroke = "#78350f",
    .surface = "#f8fafc",
    .grid_stroke = "#ffffff",
    .overlay_stroke = "#1f2937",
    .non_interacting_overlay_stroke = "#334155",
    .unavailable = "#d1d5db",
    .heatmap_saturation_percent = 72,
    .heatmap_lightness_percent = 48,
};

inline constexpr Palette dark_mode_palette{
    .background = "#D3D3D3",
    .row_fill = "#1e293b",
    .row_stroke = "#64748b",
    .movable_fill = "#60a5fa",
    .macro_fill = "#ffffff",
    .macro_stroke = "#cbd5e1",
    .fixed_fill = "#ffffff",
    .fixed_stroke = "#ffffff",
    .fixed_non_interacting_fill = "#fbbf24",
    .fixed_non_interacting_stroke = "#fde68a",
    .surface = "#0f172a",
    .grid_stroke = "#0f172a",
    .overlay_stroke = "#cbd5e1",
    .non_interacting_overlay_stroke = "#94a3b8",
    .unavailable = "#374151",
    .heatmap_saturation_percent = 78,
    .heatmap_lightness_percent = 56,
};

[[nodiscard]] constexpr const Palette &palette(bool dark_mode) { return dark_mode ? dark_mode_palette : default_palette; }

} // namespace placement::rendering_style
