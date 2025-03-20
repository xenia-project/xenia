/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/ucode.h"

namespace xe {
namespace gpu {
namespace ucode {

constexpr AluScalarOpcodeInfo kAluScalarOpcodeInfos[64] = {
    {"adds", 1, true, kAluOpChangedStateNone},
    {"adds_prev", 1, false, kAluOpChangedStateNone},
    {"muls", 1, true, kAluOpChangedStateNone},
    {"muls_prev", 1, false, kAluOpChangedStateNone},
    {"muls_prev2", 1, true, kAluOpChangedStateNone},
    {"maxs", 1, true, kAluOpChangedStateNone},
    {"mins", 1, true, kAluOpChangedStateNone},
    {"seqs", 1, false, kAluOpChangedStateNone},
    {"sgts", 1, false, kAluOpChangedStateNone},
    {"sges", 1, false, kAluOpChangedStateNone},
    {"snes", 1, false, kAluOpChangedStateNone},
    {"frcs", 1, false, kAluOpChangedStateNone},
    {"truncs", 1, false, kAluOpChangedStateNone},
    {"floors", 1, false, kAluOpChangedStateNone},
    {"exp", 1, false, kAluOpChangedStateNone},
    {"logc", 1, false, kAluOpChangedStateNone},
    {"log", 1, false, kAluOpChangedStateNone},
    {"rcpc", 1, false, kAluOpChangedStateNone},
    {"rcpf", 1, false, kAluOpChangedStateNone},
    {"rcp", 1, false, kAluOpChangedStateNone},
    {"rsqc", 1, false, kAluOpChangedStateNone},
    {"rsqf", 1, false, kAluOpChangedStateNone},
    {"rsq", 1, false, kAluOpChangedStateNone},
    {"maxas", 1, true, kAluOpChangedStateAddressRegister},
    {"maxasf", 1, true, kAluOpChangedStateAddressRegister},
    {"subs", 1, true, kAluOpChangedStateNone},
    {"subs_prev", 1, false, kAluOpChangedStateNone},
    {"setp_eq", 1, false, kAluOpChangedStatePredicate},
    {"setp_ne", 1, false, kAluOpChangedStatePredicate},
    {"setp_gt", 1, false, kAluOpChangedStatePredicate},
    {"setp_ge", 1, false, kAluOpChangedStatePredicate},
    {"setp_inv", 1, false, kAluOpChangedStatePredicate},
    {"setp_pop", 1, false, kAluOpChangedStatePredicate},
    {"setp_clr", 0, false, kAluOpChangedStatePredicate},
    {"setp_rstr", 1, false, kAluOpChangedStatePredicate},
    {"kills_eq", 1, false, kAluOpChangedStatePixelKill},
    {"kills_gt", 1, false, kAluOpChangedStatePixelKill},
    {"kills_ge", 1, false, kAluOpChangedStatePixelKill},
    {"kills_ne", 1, false, kAluOpChangedStatePixelKill},
    {"kills_one", 1, false, kAluOpChangedStatePixelKill},
    {"sqrt", 1, false, kAluOpChangedStateNone},
    {"opcode_41", 0, false, kAluOpChangedStateNone},
    {"mulsc", 2, false, kAluOpChangedStateNone},
    {"mulsc", 2, false, kAluOpChangedStateNone},
    {"addsc", 2, false, kAluOpChangedStateNone},
    {"addsc", 2, false, kAluOpChangedStateNone},
    {"subsc", 2, false, kAluOpChangedStateNone},
    {"subsc", 2, false, kAluOpChangedStateNone},
    {"sin", 1, false, kAluOpChangedStateNone},
    {"cos", 1, false, kAluOpChangedStateNone},
    {"retain_prev", 0, false, kAluOpChangedStateNone},
    {"opcode_51", 0, false, kAluOpChangedStateNone},
    {"opcode_52", 0, false, kAluOpChangedStateNone},
    {"opcode_53", 0, false, kAluOpChangedStateNone},
    {"opcode_54", 0, false, kAluOpChangedStateNone},
    {"opcode_55", 0, false, kAluOpChangedStateNone},
    {"opcode_56", 0, false, kAluOpChangedStateNone},
    {"opcode_57", 0, false, kAluOpChangedStateNone},
    {"opcode_58", 0, false, kAluOpChangedStateNone},
    {"opcode_59", 0, false, kAluOpChangedStateNone},
    {"opcode_60", 0, false, kAluOpChangedStateNone},
    {"opcode_61", 0, false, kAluOpChangedStateNone},
    {"opcode_62", 0, false, kAluOpChangedStateNone},
    {"opcode_63", 0, false, kAluOpChangedStateNone},
};

constexpr AluVectorOpcodeInfo kAluVectorOpcodeInfos[32] = {
    {"add", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"mul", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"max", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"min", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"seq", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"sgt", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"sge", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"sne", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"frc", {0b1111}, kAluOpChangedStateNone},
    {"trunc", {0b1111}, kAluOpChangedStateNone},
    {"floor", {0b1111}, kAluOpChangedStateNone},
    {"mad", {0b1111, 0b1111, 0b1111}, kAluOpChangedStateNone},
    {"cndeq", {0b1111, 0b1111, 0b1111}, kAluOpChangedStateNone},
    {"cndge", {0b1111, 0b1111, 0b1111}, kAluOpChangedStateNone},
    {"cndgt", {0b1111, 0b1111, 0b1111}, kAluOpChangedStateNone},
    {"dp4", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"dp3", {0b0111, 0b0111}, kAluOpChangedStateNone},
    {"dp2add", {0b0011, 0b0011, 0b0001}, kAluOpChangedStateNone},
    {"cube", {0b1111, 0b1111}, kAluOpChangedStateNone},
    {"max4", {0b1111}, kAluOpChangedStateNone},
    {"setp_eq_push", {0b1001, 0b1001}, kAluOpChangedStatePredicate},
    {"setp_ne_push", {0b1001, 0b1001}, kAluOpChangedStatePredicate},
    {"setp_gt_push", {0b1001, 0b1001}, kAluOpChangedStatePredicate},
    {"setp_ge_push", {0b1001, 0b1001}, kAluOpChangedStatePredicate},
    {"kill_eq", {0b1111, 0b1111}, kAluOpChangedStatePixelKill},
    {"kill_gt", {0b1111, 0b1111}, kAluOpChangedStatePixelKill},
    {"kill_ge", {0b1111, 0b1111}, kAluOpChangedStatePixelKill},
    {"kill_ne", {0b1111, 0b1111}, kAluOpChangedStatePixelKill},
    {"dst", {0b0110, 0b1010}, kAluOpChangedStateNone},
    {"maxa", {0b1111, 0b1111}, kAluOpChangedStateAddressRegister},
    {"opcode_30", {}, kAluOpChangedStateNone},
    {"opcode_31", {}, kAluOpChangedStateNone},
};

}  // namespace ucode
}  // namespace gpu
}  // namespace xe
