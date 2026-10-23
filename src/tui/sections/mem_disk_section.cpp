#include "tui/sections/mem_disk_section.h"

#include <algorithm>
#include <string>
#include <vector>

#include "tui/format.h"
#include "tui/widgets/progress_bar.h"

namespace hwmon {
namespace tui {

namespace {
using namespace ftxui;  // NOLINT(build/namespaces)

constexpr int kMemBarWidth = 16;
constexpr int kSwapBarWidth = 12;
constexpr int kDiskBarWidth = 14;
constexpr int kMountBarWidth = 12;

Element mountRow(const DiskMount& m) {
  const std::string usage =
      m.usage.valid ? bytesKiB(m.usage.value) + " / " + bytesKiB(m.usage.max) : "";
  return hbox({
      text("  "),
      text(m.mount + " (" + m.fs + ")"),
      gaugeText(m.usage, kMountBarWidth, BarColor::Capacity, usage),
  });
}

// 挂载点按设备名前缀匹配归属（如 nvme0n1p2 → nvme0n1）；取最长匹配的块设备。
const DiskBlock* owningBlock(const DiskMount& m, const std::vector<DiskBlock>& blocks) {
  const DiskBlock* best = nullptr;
  for (const DiskBlock& b : blocks) {
    if (m.dev.empty() || b.dev.empty())
      continue;
    if (m.dev.rfind(b.dev, 0) != 0)
      continue;
    if (best == nullptr || b.dev.size() > best->dev.size())
      best = &b;
  }
  return best;
}

}  // namespace

Element memDiskSection(const MemView& mem, const DiskView& disk, int termWidth) {
  (void)termWidth;
  Elements rows;

  // 内存行：内存 [条] 21.3G / 31.2G   SWAP [条] 1.2G / 8.0G（无 swap → invalid 留白）
  rows.push_back(hbox({
      text("内存 "),
      gaugeText(mem.latest.used, kMemBarWidth, BarColor::Capacity,
                mem.latest.used.valid ? bytesKiB(mem.latest.used.value) + " / " +
                                            bytesKiB(mem.latest.used.max)
                                      : ""),
      text("   "),
      text("SWAP "),
      gaugeText(mem.latest.swap, kSwapBarWidth, BarColor::Capacity,
                mem.latest.swap.valid ? bytesKiB(mem.latest.swap.value) + " / " +
                                            bytesKiB(mem.latest.swap.max)
                                      : ""),
  }));

  // 块设备行 + 归属挂载点行
  const std::vector<DiskBlock>& blocks = disk.blocks;
  std::vector<std::vector<const DiskMount*>> under(blocks.size());
  std::vector<const DiskMount*> other;  // 归属不明 → “其他挂载”组
  for (const DiskMount& m : disk.mounts) {
    const DiskBlock* owner = owningBlock(m, blocks);
    if (owner != nullptr)
      under[static_cast<size_t>(owner - blocks.data())].push_back(&m);
    else
      other.push_back(&m);
  }

  for (size_t i = 0; i < blocks.size(); ++i) {
    const DiskBlock& b = blocks[i];
    const double rBps = i < disk.readBps.size() ? disk.readBps[i] : -1.0;
    const double wBps = i < disk.writeBps.size() ? disk.writeBps[i] : -1.0;
    // usage invalid → 只显示总量（容量条本身由 gaugeBar 降级为 —）
    const std::string cap =
        b.usage.valid ? bytesKiB(b.usage.value) + "/" + bytesKiB(static_cast<double>(b.sizeKiB))
                      : bytesKiB(static_cast<double>(b.sizeKiB));
    rows.push_back(hbox({
        text(i == 0 ? "硬盘 " : "     "),
        text(b.dev + " "),
        gaugeText(b.usage, kDiskBarWidth, BarColor::Capacity, ""),
        text(" " + cap),
        text("  R " + bytesPerSec(rBps) + " · W " + bytesPerSec(wBps)),
    }));
    for (const DiskMount* m : under[i])
      rows.push_back(mountRow(*m));
  }

  if (!other.empty()) {
    rows.push_back(text("其他挂载") | dim);
    for (const DiskMount* m : other)
      rows.push_back(mountRow(*m));
  }

  return vbox(std::move(rows));
}

}  // namespace tui
}  // namespace hwmon
