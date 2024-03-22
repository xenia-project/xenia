/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_GAME_INFO_DATABASE_H_
#define XENIA_KERNEL_UTIL_GAME_INFO_DATABASE_H_

#include <string>
#include <vector>

#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/util/xlast.h"

namespace xe {
namespace kernel {
namespace util {

class GameInfoDatabase {
 public:
  struct Context {
    uint32_t id;
    uint32_t max_value;
    uint32_t default_value;
    std::string description;
  };

  struct Property {
    uint32_t id;
    uint32_t data_size;
    std::string description;
  };

  struct Achievement {
    uint32_t id;
    std::string label;
    std::string description;
    std::string unachieved_description;
    uint32_t image_id;
    uint32_t gamerscore;
    uint32_t flags;
  };

  struct Query {
    uint32_t id;
    std::string name;
    std::vector<uint32_t> input_parameters;
    std::vector<uint32_t> filters;
    std::vector<uint32_t> expected_return;
  };

  struct Filter {
    uint32_t left_id;
    uint32_t right_id;
    std::string comparation_operator;
  };

  struct Field {
    uint32_t ordinal;
    std::string name;
    bool is_hidden;
    uint16_t attribute_id;
    // std::map<property_id, aggregation string>
    std::map<uint32_t, std::string> property_aggregation;
  };

  struct StatsView {
    uint32_t id;
    std::string name;
    std::vector<Field> fields;
  };

  struct ProductInformation {
    uint32_t max_offline_players_count;
    uint32_t max_systemlink_players_count;
    uint32_t max_live_players_count;
    std::string publisher_name;
    std::string developer_name;
    std::string marketing_info;
    std::string genre_description;
    std::vector<std::string> features;
  };

  // Normally titles have at least XDBF file embedded into xex. There are
  // certain exceptions and that's why we need to check if it is even valid.
  GameInfoDatabase(const XdbfGameData* data);
  ~GameInfoDatabase();

  bool IsValid() const { return is_valid_; }

  // This is mostly extracted from XDBF.
  std::string GetTitleName(
      const XLanguage language = XLanguage::kInvalid) const;

  XLanguage GetDefaultLanguage() const;
  std::string GetLocalizedString(
      const uint32_t id, XLanguage language = XLanguage::kInvalid) const;

  std::vector<uint8_t> GetIcon() const;

  Context GetContext(const uint32_t id) const;
  Property GetProperty(const uint32_t id) const;
  Achievement GetAchievement(const uint32_t id) const;

  // TODO: Implement it in the future.
  StatsView GetStatsView(const uint32_t id) const;
  std::vector<uint32_t> GetMatchmakingAttributes(const uint32_t id) const;

  // This is extracted from XLast.
  Query GetQueryData(const uint32_t id) const;
  std::vector<XLanguage> GetSupportedLanguages() const;
  ProductInformation GetProductInformation() const;

  // Aggregators for specific usecases
  std::vector<Context> GetContexts() const;
  std::vector<Property> GetProperties() const;
  std::vector<Achievement> GetAchievements() const;
  // TODO: Implement it in the future.
  std::vector<StatsView> GetStatsViews() const;

 private:
  bool is_valid_ = false;
  std::unique_ptr<XdbfGameData> xdbf_gamedata_;
  std::unique_ptr<XLast> xlast_gamedata_;
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_GAME_INFO_DATABASE_H_