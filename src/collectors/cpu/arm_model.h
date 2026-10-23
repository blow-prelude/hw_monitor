#pragma once
// ARM implementer/part 解码表（02-collectors §4）。纯查表函数，可单测。
namespace hwmon {

// (implementer, part) → 核型号名（如 0x41/0xd0a → "Cortex-A75"）；未命中返回 nullptr。
const char* armCoreName(unsigned implementer, unsigned part);

// implementer id → 厂商名（如 0x41 → "ARM"）；未命中返回 nullptr。
const char* armImplementerName(unsigned implementer);

}  // namespace hwmon
