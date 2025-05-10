/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PNG_UTILS_H_
#define XENIA_BASE_PNG_UTILS_H_

#include <filesystem>
#include <utility>
#include <vector>

namespace xe {

bool IsFilePngImage(const std::filesystem::path& file_path);
std::pair<uint16_t, uint16_t> GetImageResolution(
    const std::filesystem::path& file_path);

std::vector<uint8_t> ReadPngFromFile(const std::filesystem::path& file_path);

}  // namespace xe

#endif  // XENIA_BASE_PNG_UTILS_H_
