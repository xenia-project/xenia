/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xlast.h"
#include "third_party/zlib/zlib.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace kernel {
namespace util {

XLastMatchmakingQuery::XLastMatchmakingQuery() {}
XLastMatchmakingQuery::XLastMatchmakingQuery(
    const pugi::xpath_node query_node) {
  node_ = query_node;
}

std::string XLastMatchmakingQuery::GetName() const {
  return node_.node().attribute("friendlyName").value();
}

std::vector<uint32_t> XLastMatchmakingQuery::GetReturns() const {
  return XLast::GetAllValuesFromNode(node_, "Returns", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetParameters() const {
  return XLast::GetAllValuesFromNode(node_, "Parameters", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetFilters() const {
  return XLast::GetAllValuesFromNode(node_, "Filters", "left");
}

XLast::XLast() : parsed_xlast_(nullptr) {}

XLast::XLast(const uint8_t* compressed_xml_data,
             const uint32_t compressed_data_size,
             const uint32_t decompressed_data_size) {
  if (!compressed_data_size || !decompressed_data_size) {
    XELOGW("XLast: Current title don't have any XLast XML data!");
    return;
  }

  parsed_xlast_ = std::make_unique<pugi::xml_document>();
  xlast_decompressed_xml_.resize(decompressed_data_size);

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;
  stream.avail_in = 0;
  stream.next_in = Z_NULL;

  int ret = inflateInit2(
      &stream, 16 + MAX_WBITS);  // 16 + MAX_WBITS enables gzip decoding
  if (ret != Z_OK) {
    XELOGE("XLast: Error during Zlib stream init");
    return;
  }

  stream.avail_in = compressed_data_size;
  stream.next_in =
      reinterpret_cast<Bytef*>(const_cast<uint8_t*>(compressed_xml_data));
  stream.avail_out = decompressed_data_size;
  stream.next_out = reinterpret_cast<Bytef*>(xlast_decompressed_xml_.data());

  ret = inflate(&stream, Z_NO_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    XELOGE("XLast: Error during XLast decompression");
    inflateEnd(&stream);
    return;
  }
  inflateEnd(&stream);

  parse_result_ = parsed_xlast_->load_buffer(xlast_decompressed_xml_.data(),
                                             xlast_decompressed_xml_.size());
}

XLast::~XLast() {}

std::u16string XLast::GetTitleName() const {
  std::string xpath = "/XboxLiveSubmissionProject/GameConfigProject";

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return std::u16string();
  }

  return xe::to_utf16(node.node().attribute("titleName").value());
}

std::map<ProductInformationEntry, uint32_t>
XLast::GetProductInformationAttributes() const {
  std::map<ProductInformationEntry, uint32_t> attributes;

  std::string xpath =
      "/XboxLiveSubmissionProject/GameConfigProject/ProductInformation";

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return attributes;
  }

  const auto node_attributes = node.node().attributes();
  for (const auto& attribute : node_attributes) {
    const auto entry =
        product_information_entry_string_to_enum.find(attribute.name());
    if (entry == product_information_entry_string_to_enum.cend()) {
      XELOGW("GetProductInformationAttributes: Missing attribute: {}",
             attribute.name());
      continue;
    }

    std::string attribute_value = std::string(attribute.value());
    if (attribute_value.empty()) {
      XELOGW(
          "GetProductInformationAttributes: Attribute: {} Contains no value!",
          attribute.name());
      continue;
    }

    attributes.emplace(entry->second,
                       xe::string_util::from_string<uint32_t>(attribute_value));
  }

  return attributes;
}

std::vector<XLanguage> XLast::GetSupportedLanguages() const {
  std::vector<XLanguage> launguages;

  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings");

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return launguages;
  }

  const auto locale = node.node().children("SupportedLocale");
  for (auto itr = locale.begin(); itr != locale.end(); itr++) {
    const std::string locale_name = itr->attribute("locale").value();

    for (const auto& language : language_mapping) {
      if (language.second == locale_name) {
        launguages.push_back(language.first);
      }
    }
  }

  return launguages;
}

std::u16string XLast::GetLocalizedString(uint32_t string_id,
                                         XLanguage language) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings/"
      "LocalizedString[@id = \"{}\"]",
      string_id);

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return std::u16string();
  }

  const std::string locale_name = GetLocaleStringFromLanguage(language);
  const pugi::xml_node locale_node =
      node.node().find_child_by_attribute("locale", locale_name.c_str());

  if (!locale_node) {
    return std::u16string();
  }

  return xe::to_utf16(locale_node.child_value());
}

XLastMatchmakingQuery* XLast::GetMatchmakingQuery(
    const uint32_t query_id) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Matchmaking/Queries/"
      "Query[@id = \"{}\"]",
      query_id);

  XLastMatchmakingQuery* query = nullptr;
  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return query;
  }

  return new XLastMatchmakingQuery(node);
}

std::vector<uint32_t> XLast::GetAllValuesFromNode(
    const pugi::xpath_node node, const std::string child_name,
    const std::string attirbute_name) {
  std::vector<uint32_t> result{};

  const auto searched_child = node.node().child(child_name.c_str());

  for (pugi::xml_node_iterator itr = searched_child.begin();
       itr != searched_child.end(); itr++) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        itr->attribute(attirbute_name.c_str()).value(), true));
  }

  return result;
}

void XLast::Dump(std::string file_name) const {
  if (xlast_decompressed_xml_.empty()) {
    return;
  }

  if (file_name.empty()) {
    file_name = xe::to_utf8(GetTitleName());
  }

  FILE* outfile =
      xe::filesystem::OpenFile(fmt::format("{}.xml", file_name).c_str(), "ab");
  if (!outfile) {
    return;
  }

  fwrite(xlast_decompressed_xml_.data(), 1, xlast_decompressed_xml_.size(),
         outfile);
  fclose(outfile);
}

std::string XLast::GetLocaleStringFromLanguage(XLanguage language) const {
  const auto value = language_mapping.find(language);
  if (value != language_mapping.cend()) {
    return value->second;
  }

  return language_mapping.at(XLanguage::kEnglish);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe