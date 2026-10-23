#include "collectors/cpu/arm_model.h"

#include <cstddef>
#include <utility>

namespace hwmon {
namespace {

struct IdName { unsigned id; const char* name; };

// 收录常见项（02-collectors §4：ARM A53/A55/A35/A72/A75/A76/A78/X1/Neoverse、
// 高通、海思、NVIDIA Denver…）。未命中由调用方回退 hex 原文。
constexpr IdName kImplementers[] = {
    {0x41, "ARM"},      {0x42, "Broadcom"}, {0x43, "Cavium"},   {0x44, "DEC"},
    {0x46, "Fujitsu"},  {0x48, "HiSilicon"},{0x49, "Infineon"}, {0x4d, "Motorola"},
    {0x4e, "NVIDIA"},   {0x50, "APM"},      {0x51, "Qualcomm"}, {0x53, "Samsung"},
    {0x56, "Marvell"},  {0x61, "Apple"},    {0x66, "Faraday"},  {0x69, "Intel"},
    {0x70, "Phytium"},  {0xc0, "Ampere"},
};

constexpr IdName kArmParts[] = {
    {0xc05, "Cortex-A5"},   {0xc07, "Cortex-A7"},    {0xc08, "Cortex-A8"},
    {0xc09, "Cortex-A9"},   {0xc0f, "Cortex-A15"},   {0xc14, "Cortex-A17"},
    {0xd01, "Cortex-A32"},  {0xd03, "Cortex-A53"},   {0xd04, "Cortex-A35"},
    {0xd05, "Cortex-A55"},  {0xd06, "Cortex-A65"},   {0xd07, "Cortex-A57"},
    {0xd08, "Cortex-A72"},  {0xd09, "Cortex-A73"},   {0xd0a, "Cortex-A75"},
    {0xd0b, "Cortex-A76"},  {0xd0c, "Neoverse-N1"},  {0xd0d, "Cortex-A77"},
    {0xd0e, "Cortex-A76AE"},{0xd13, "Cortex-R52"},   {0xd40, "Neoverse-V1"},
    {0xd41, "Cortex-A78"},  {0xd42, "Cortex-A78AE"}, {0xd44, "Cortex-X1"},
    {0xd47, "Neoverse-N2"}, {0xd48, "Cortex-X2"},    {0xd4d, "Cortex-A710"},
    {0xd4e, "Cortex-A510"}, {0xd4f, "Neoverse-V2"},
};

constexpr IdName kQualcommParts[] = {
    {0x02d, "Scorpion"},    {0x04d, "Krait"},       {0x06f, "Krait"},
    {0x800, "Kryo 2xx Gold"}, {0x801, "Kryo 2xx Silver"},
    {0x802, "Kryo 2xx Gold"}, {0x803, "Kryo 2xx Silver"},
    {0x804, "Kryo 3xx Gold"}, {0x805, "Kryo 3xx Silver"},
    {0xc00, "Falkor"},      {0xc01, "Saphira"},
};

constexpr IdName kHisiliconParts[] = {
    {0xd01, "TaiShan-v110"}, {0xd02, "TaiShan-v120"},
};

constexpr IdName kNvidiaParts[] = {
    {0x003, "Denver"}, {0x004, "Denver 2"}, {0x00a, "Carmel"},
};

constexpr IdName kAppleParts[] = {
    {0x022, "Icestorm"}, {0x023, "Firestorm"},
    {0x032, "Blizzard"}, {0x033, "Avalanche"},
};

constexpr IdName kCaviumParts[] = {
    {0x0a1, "ThunderX"}, {0x0af, "ThunderX2"},
};

constexpr IdName kFujitsuParts[] = {
    {0x001, "A64FX"},
};

const char* lookup(std::size_t n, const IdName* table, unsigned id) {
    for (std::size_t i = 0; i < n; ++i)
        if (table[i].id == id) return table[i].name;
    return nullptr;
}

}  // namespace

const char* armCoreName(unsigned implementer, unsigned part) {
    switch (implementer) {
        case 0x41:      return lookup(sizeof(kArmParts) / sizeof(kArmParts[0]), kArmParts, part);
        case 0x51:      return lookup(sizeof(kQualcommParts) / sizeof(kQualcommParts[0]), kQualcommParts, part);
        case 0x48:      return lookup(sizeof(kHisiliconParts) / sizeof(kHisiliconParts[0]), kHisiliconParts, part);
        case 0x4e:      return lookup(sizeof(kNvidiaParts) / sizeof(kNvidiaParts[0]), kNvidiaParts, part);
        case 0x61:      return lookup(sizeof(kAppleParts) / sizeof(kAppleParts[0]), kAppleParts, part);
        case 0x43:      return lookup(sizeof(kCaviumParts) / sizeof(kCaviumParts[0]), kCaviumParts, part);
        case 0x46:      return lookup(sizeof(kFujitsuParts) / sizeof(kFujitsuParts[0]), kFujitsuParts, part);
        default:        return nullptr;
    }
}

const char* armImplementerName(unsigned implementer) {
    return lookup(sizeof(kImplementers) / sizeof(kImplementers[0]), kImplementers, implementer);
}

}  // namespace hwmon
