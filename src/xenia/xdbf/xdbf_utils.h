/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_XDBF_XDBF_UTILS_H_
#define XENIA_XDBF_XDBF_UTILS_H_

#include <vector>
#include <string>

#include "xenia/base/memory.h"

namespace xe {
namespace xdbf {
	
	// http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/XEX/SPA.h
	// http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/XEX/SPA.cpp

	enum XdbfSection : uint16_t {
		kSectionMetadata = 0x0001,
		kSectionImage = 0x0002,
		kSectionStringTable = 0x0003,
	};

	enum XdbfLocale : uint32_t {
		kLocaleDefault = 0,
		kLocaleEnglish = 1,
		kLocaleJapanese = 2,
	};

	struct XBDF_HEADER {
		xe::be<uint32_t> magic;
		xe::be<uint32_t> version;
		xe::be<uint32_t> entry_max;
		xe::be<uint32_t> entry_current;
		xe::be<uint32_t> free_max;
		xe::be<uint32_t> free_current;
	};
	static_assert_size(XBDF_HEADER, 24);

#pragma pack(push, 1)
	struct XBDF_ENTRY {
		xe::be<uint16_t> section;
		xe::be<uint64_t> id;
		xe::be<uint32_t> offset;
		xe::be<uint32_t> size;
	};
	static_assert_size(XBDF_ENTRY, 18);
#pragma pack(pop)

	struct XBDF_FILE_LOC {
		xe::be<uint32_t> offset;
		xe::be<uint32_t> size;
	};
	static_assert_size(XBDF_FILE_LOC, 8);

	struct XDBF_XSTC {
		xe::be<uint32_t> magic;
		xe::be<uint32_t> version;
		xe::be<uint32_t> size;
		xe::be<uint32_t> default_language;
	};
	static_assert_size(XDBF_XSTC, 16);

#pragma pack(push, 1)
	struct XDBF_XSTR_HEADER {
		xe::be<uint32_t> magic;
		xe::be<uint32_t> version;
		xe::be<uint32_t> size;
		xe::be<uint16_t> string_count;
	};
	static_assert_size(XDBF_XSTR_HEADER, 14);
#pragma pack(pop)

	struct XDBF_STRINGTABLE_ENTRY {
		xe::be<uint16_t> id;
		xe::be<uint16_t> string_length;
	};
	static_assert_size(XDBF_STRINGTABLE_ENTRY, 4);

	struct XdbfState {
		XBDF_HEADER* header;
		XBDF_ENTRY* entries;
		XBDF_FILE_LOC* files;
		uint8_t* offset;
		uint8_t* data;
		size_t size;
	};

	struct XdbfBlock {
		uint8_t* buffer;
		size_t size;
	};

	class XdbfWrapper {
	public:
		XdbfWrapper();

		bool initialize(uint8_t* buffer, size_t length);
		XdbfBlock get_entry(XdbfSection section, uint64_t id) const;

	private:
		XBDF_HEADER& get_header() const;
		XBDF_ENTRY& get_entry(uint32_t n) const;
		XBDF_FILE_LOC& get_file(uint32_t n) const;

		XdbfState state_;
	};

	XdbfBlock get_icon(XdbfWrapper& ref);
	std::string get_title(XdbfWrapper& ref);


}  // namespace xdbf
}  // namespace xe

#endif  // XENIA_XDBF_XDBF_UTILS_H_
