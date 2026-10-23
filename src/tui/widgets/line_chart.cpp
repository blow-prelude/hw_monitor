#include "tui/widgets/line_chart.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "tui/format.h"

namespace hwmon {
namespace tui {

namespace {

// FTXUI Canvas 坐标为 braille 亚像素：1 字符格 = 2×4 点。
constexpr int kCellDotsX = 2;
constexpr int kCellDotsY = 4;

// nice 化：向上取整到 1/2/5×10ⁿ（03-tui.md §7 第 1 步）；全无效（m<=0）→ 1
double niceCeil(double m) {
  if (m <= 0)
    return 1;
  const int k = static_cast<int>(std::floor(std::log10(m)));
  const double mant = m / std::pow(10.0, k);
  const double f = mant <= 1 ? 1 : (mant <= 2 ? 2 : (mant <= 5 ? 5 : 10));
  return f * std::pow(10.0, k);
}

// 每列取窗口内有效点的最大值（降采样时尖峰不被抹掉）；窗口全无效 → 断点。
struct Column {
  double value = 0;
  bool ok = false;
};

std::vector<Column> toColumns(const std::vector<double>& pts, int stride, int nCols) {
  std::vector<Column> cols(static_cast<size_t>(nCols));
  for (int c = 0; c < nCols; ++c) {
    const size_t begin = static_cast<size_t>(c) * stride;
    const size_t end = std::min(pts.size(), begin + static_cast<size_t>(stride));
    for (size_t i = begin; i < end; ++i) {
      if (pts[i] < 0)
        continue;
      if (!cols[c].ok || pts[i] > cols[c].value) {
        cols[c].value = pts[i];
        cols[c].ok = true;
      }
    }
  }
  return cols;
}

// 画一条时间序：一列一格，点数 < 窗口宽右对齐（最新点贴右缘，空位留白）；
// 相邻两点任一为 -1 → 断线不连接（03-tui.md §7 第 3/5 步）。
// cellsX 为可用列数（格），Canvas 内部按点坐标绘制。
void drawSeries(ftxui::Canvas& cv, const std::vector<double>& pts, double nice, int cellsX,
                int dotH, ftxui::Color color) {
  if (pts.empty() || cellsX <= 0)
    return;
  int stride = 1;
  if (static_cast<int>(pts.size()) > cellsX)
    stride = static_cast<int>((pts.size() + static_cast<size_t>(cellsX) - 1) / cellsX);
  const int nCols =
      static_cast<int>((pts.size() + static_cast<size_t>(stride) - 1) / stride);
  const int x0 = cellsX - nCols;  // 右对齐

  const std::vector<Column> cols = toColumns(pts, stride, nCols);
  int prevX = 0, prevY = 0;
  bool prevOk = false;
  for (int k = 0; k < nCols; ++k) {
    if (!cols[k].ok) {
      prevOk = false;  // 断点
      continue;
    }
    // y = value/nice*height；Canvas y 轴向下，绘制时翻转（03-tui.md §7 第 2 步）
    const double yNorm = std::clamp(cols[k].value / nice, 0.0, 1.0);
    const int x = (x0 + k) * kCellDotsX;
    const int y =
        std::clamp(dotH - 1 - static_cast<int>(std::lround(yNorm * (dotH - 1))), 0, dotH - 1);
    if (prevOk)
      cv.DrawPointLine(prevX, prevY, x, y, color);
    else
      cv.DrawPoint(x, y, true, color);  // 断点后的孤立点也可见
    prevX = x;
    prevY = y;
    prevOk = true;
  }
}

}  // namespace

ftxui::Element throughputChart(const Series& rx, const Series& tx, int widthCells,
                               int heightCells) {
  using namespace ftxui;  // NOLINT(build/namespaces) — FTXUI 惯用用法

  const int w = std::max(widthCells, 12);   // 格
  const int h = std::max(heightCells, 2);   // 格
  const int dotW = w * kCellDotsX;
  const int dotH = h * kCellDotsY;

  // 1. 两组数据有效点最大值 → nice 化；全无效 → nice = 1，画空坐标系
  double m = 0;
  bool any = false;
  for (const double p : rx.points) {
    if (p >= 0) {
      any = true;
      m = std::max(m, p);
    }
  }
  for (const double p : tx.points) {
    if (p >= 0) {
      any = true;
      m = std::max(m, p);
    }
  }
  const double nice = niceCeil(m);

  Canvas cv(dotW, dotH);
  if (any) {
    drawSeries(cv, rx.points, nice, w, dotH, Color::Green);
    drawSeries(cv, tx.points, nice, w, dotH, Color::Cyan);
  }

  // 顶部标注：左上 nice 峰值，右上 (120s)；后画覆盖折线，避免被点阵破坏
  // （Canvas 内只画线与峰值，图例/当前值在图上方文字行，03-tui.md §7 第 4 步）
  const std::string peak = any ? bytesPerSec(nice) : "";
  const std::string window = "(120s)";
  const int peakCells = static_cast<int>(peak.size());
  const int windowCells = static_cast<int>(window.size());
  if (peakCells > 0 && 1 + peakCells < w)
    cv.DrawText(kCellDotsX, 0, peak, Color::GrayLight);
  if (w - 1 - windowCells > 1 + peakCells + 1)  // 两标注不相交且不贴左缘
    cv.DrawText((w - 1 - windowCells) * kCellDotsX, 0, window, Color::GrayLight);

  Element chart = canvas(ConstRef<Canvas>(std::move(cv)));
  if (!any) {
    // 空坐标系 + “等待数据…”（03-tui.md §10）
    return dbox({
               std::move(chart),
               vbox({filler(), text("等待数据…") | hcenter | dim, filler()}),
           }) |
           border;
  }
  return std::move(chart) | border;
}

}  // namespace tui
}  // namespace hwmon
