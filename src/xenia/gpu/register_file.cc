/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/register_file.h"
#include <array>
#include <cstring>

#include "xenia/base/math.h"

namespace xe {
namespace gpu {

RegisterFile::RegisterFile() { std::memset(values, 0, sizeof(values)); }
constexpr unsigned int GetHighestRegisterNumber() {
  uint32_t highest = 0;
#define XE_GPU_REGISTER(index, type, name) \
  highest = std::max<uint32_t>(highest, index);
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER

  return highest;
}
constexpr unsigned int GetLowestRegisterNumber() {
  uint32_t lowest = UINT_MAX;
#define XE_GPU_REGISTER(index, type, name) \
  lowest = std::min<uint32_t>(lowest, index);
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER

  return lowest;
}

static constexpr uint32_t lowest_register = GetLowestRegisterNumber();
static constexpr uint32_t highest_register = GetHighestRegisterNumber();

static constexpr uint32_t total_num_registers =
    highest_register - lowest_register;

static constexpr uint32_t num_required_words_for_registers =
    ((total_num_registers + 63) & ~63) / 64;
// can't use bitset, its not constexpr in c++ 17
using ValidRegisterBitset = std::array<
    uint64_t,
    num_required_words_for_registers>;  // std::bitset<highest_register
                                        // - lowest_register>;

static constexpr ValidRegisterBitset BuildValidRegisterBitset() {
  ValidRegisterBitset result{};
#define XE_GPU_REGISTER(index, type, name)  \
  result[(index - lowest_register) / 64] |= \
      1ULL << ((index - lowest_register) % 64);

#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER

  return result;
}
static constexpr ValidRegisterBitset valid_register_bitset =
    BuildValidRegisterBitset();

const RegisterInfo* RegisterFile::GetRegisterInfo(uint32_t index) {
  switch (index) {
#define XE_GPU_REGISTER(index, type, name) \
  case index: {                            \
    static const RegisterInfo reg_info = { \
        RegisterInfo::Type::type,          \
        #name,                             \
    };                                     \
    return &reg_info;                      \
  }
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER
    default:
      return nullptr;
  }
}
/*
        todo: this still uses a lot of cpu! our bitset is too large
*/
bool RegisterFile::IsValidRegister(uint32_t index) {
  if (XE_UNLIKELY(index < lowest_register) ||
      XE_UNLIKELY(index > highest_register)) {
    return false;
  }
  uint32_t register_linear_index = index - lowest_register;

  return (valid_register_bitset[register_linear_index / 64] &
          (1ULL << (register_linear_index % 64))) != 0;
}
}  //  namespace gpu
}  //  namespace xe
