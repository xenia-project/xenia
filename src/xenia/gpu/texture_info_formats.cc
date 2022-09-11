/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// clang-format off

#include "xenia/gpu/texture_info.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

#define FORMAT_INFO(texture_format, format, block_width, block_height, bits_per_pixel) \
    {xenos::TextureFormat::texture_format,  FormatType::format, block_width, block_height, bits_per_pixel}
const FormatInfo* FormatInfo::Get(uint32_t gpu_format) {
  static const FormatInfo format_infos[64] = {
      #include "texture_info_formats.inl"
  };
  return &format_infos[gpu_format];
}
#undef FORMAT_INFO


constexpr unsigned char GetShift(unsigned pow) {
	unsigned char sh = 0;

	while (!(pow & 1)) {
		pow>>=1;
		sh++;
	}

	return sh;
}
/*
	todo: getwidthshift and getheightshift should not need a full 64 byte table each
	there are 15 elements for GetWidthShift where the shift will not be 0. the max shift that will be returned is 2, and there are 64 elements total
	this means we can use a boolean table that also acts as a sparse indexer ( popcnt preceding bits to get index) and shift and mask a 32 bit word to get the shift
*/
unsigned char FormatInfo::GetWidthShift(uint32_t gpu_format) {
	#define		FORMAT_INFO(texture_format, format, block_width, block_height, bits_per_pixel)		GetShift(block_width)
	alignas(XE_HOST_CACHE_LINE_SIZE)
	constexpr unsigned char wshift_table[64] = {
		#include "texture_info_formats.inl"
	};
	#undef FORMAT_INFO

	return wshift_table[gpu_format];
}
unsigned char FormatInfo::GetHeightShift(uint32_t gpu_format) {
#define		FORMAT_INFO(texture_format, format, block_width, block_height, bits_per_pixel)		GetShift(block_height)
	alignas(XE_HOST_CACHE_LINE_SIZE)
	constexpr unsigned char hshift_table[64] = {
		#include "texture_info_formats.inl"
	};
	#undef FORMAT_INFO

	return hshift_table[gpu_format];
}
#define		FORMAT_INFO(texture_format,...)		#texture_format
static constexpr const char* const format_name_table[64] = {
	#include "texture_info_formats.inl"

};
#undef FORMAT_INFO
const char* FormatInfo::GetName(uint32_t gpu_format) {

	return format_name_table[gpu_format];
}
}  //  namespace gpu
}  //  namespace xe
