// 内存 + 硬盘节渲染。权威规格：docs/design/03-tui.md §5.4。
#ifndef HWMON_TUI_SECTIONS_MEM_DISK_SECTION_H_
#define HWMON_TUI_SECTIONS_MEM_DISK_SECTION_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// 首行：内存 / SWAP 容量条；其后每块设备一行（容量条 + R/W 速率），挂载点行缩进
// 两列挂在对应块设备下（按设备名前缀匹配归属，最长匹配优先；
// 归属不明的挂载点集中放“其他挂载”组）。
ftxui::Element memDiskSection(const MemView& mem, const DiskView& disk, int termWidth);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_SECTIONS_MEM_DISK_SECTION_H_
