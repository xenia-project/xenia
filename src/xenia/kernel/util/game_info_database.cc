/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/game_info_database.h"
#include "xenia/base/logging.h"

namespace xe {
namespace kernel {
namespace util {

GameInfoDatabase::GameInfoDatabase(const XdbfGameData* data) {
  if (!data) {
    return;
  }

  if (!data->is_valid()) {
    return;
  }

  is_valid_ = true;
  xdbf_gamedata_ = std::make_unique<XdbfGameData>(*data);

  uint32_t compressed_size, decompressed_size = 0;
  const uint8_t* xlast_ptr =
      xdbf_gamedata_->ReadXLast(compressed_size, decompressed_size);

  if (!xlast_ptr) {
    XELOGW(
        "GameDatabase: Title doesn't contain XLAST data! Multiplayer "
        "functionality might be limited.");
    return;
  }

  xlast_gamedata_ =
      std::make_unique<XLast>(xlast_ptr, compressed_size, decompressed_size);

  if (!xlast_gamedata_) {
    XELOGW(
        "GameDatabase: Title XLAST data is corrupted! Multiplayer "
        "functionality might be limited.");
    return;
  }
}

GameInfoDatabase::~GameInfoDatabase() {}

std::string GameInfoDatabase::GetTitleName(const XLanguage language) const {
  if (!is_valid_) {
    return "";
  }

  return xdbf_gamedata_->title(xdbf_gamedata_->GetExistingLanguage(language));
}

std::vector<uint8_t> GameInfoDatabase::GetIcon() const {
  std::vector<uint8_t> data;

  if (!is_valid_) {
    return data;
  }

  const XdbfBlock icon = xdbf_gamedata_->icon();
  data.resize(icon.size);
  std::memcpy(data.data(), icon.buffer, icon.size);
  return data;
}

XLanguage GameInfoDatabase::GetDefaultLanguage() const {
  if (!is_valid_) {
    return XLanguage::kEnglish;
  }

  return xdbf_gamedata_->default_language();
}

std::string GameInfoDatabase::GetLocalizedString(const uint32_t id,
                                                 XLanguage language) const {
  if (!is_valid_) {
    return "";
  }

  return xdbf_gamedata_->GetStringTableEntry(
      xdbf_gamedata_->GetExistingLanguage(language), id);
}

GameInfoDatabase::Context GameInfoDatabase::GetContext(
    const uint32_t id) const {
  Context context = {};

  if (!is_valid_) {
    return context;
  }

  const auto xdbf_context = xdbf_gamedata_->GetContext(id);

  context.id = xdbf_context.id;
  context.default_value = xdbf_context.default_value;
  context.max_value = xdbf_context.max_value;
  context.description = GetLocalizedString(xdbf_context.string_id);
  return context;
}

GameInfoDatabase::Property GameInfoDatabase::GetProperty(
    const uint32_t id) const {
  Property property = {};

  if (!is_valid_) {
    return property;
  }

  const auto xdbf_property = xdbf_gamedata_->GetProperty(id);

  property.id = xdbf_property.id;
  property.data_size = xdbf_property.data_size;
  property.description = GetLocalizedString(xdbf_property.string_id);
  return property;
}

GameInfoDatabase::Achievement GameInfoDatabase::GetAchievement(
    const uint32_t id) const {
  Achievement achievement = {};

  if (!is_valid_) {
    return achievement;
  }

  const auto xdbf_achievement = xdbf_gamedata_->GetAchievement(id);

  achievement.id = xdbf_achievement.id;
  achievement.image_id = xdbf_achievement.id;
  achievement.gamerscore = xdbf_achievement.gamerscore;
  achievement.flags = xdbf_achievement.flags;

  achievement.label = GetLocalizedString(xdbf_achievement.label_id);
  achievement.description = GetLocalizedString(xdbf_achievement.description_id);
  achievement.unachieved_description =
      GetLocalizedString(xdbf_achievement.unachieved_id);
  return achievement;
}

std::vector<uint32_t> GameInfoDatabase::GetMatchmakingAttributes(
    const uint32_t id) const {
  std::vector<uint32_t> result;

  const auto xdbf_matchmaking_data = xdbf_gamedata_->GetMatchCollection();

  result.insert(result.end(), xdbf_matchmaking_data.contexts.cbegin(),
                xdbf_matchmaking_data.contexts.cend());
  result.insert(result.end(), xdbf_matchmaking_data.properties.cbegin(),
                xdbf_matchmaking_data.properties.cend());

  return result;
}

// XLAST
GameInfoDatabase::Query GameInfoDatabase::GetQueryData(
    const uint32_t id) const {
  Query query = {};

  if (!xlast_gamedata_) {
    return query;
  }

  const auto xlast_query = xlast_gamedata_->GetMatchmakingQuery(id);
  if (!xlast_query) {
    return query;
  }

  query.id = id;
  query.name = xlast_query->GetName();
  query.input_parameters = xlast_query->GetParameters();
  query.filters = xlast_query->GetFilters();
  query.expected_return = xlast_query->GetReturns();
  return query;
}

std::vector<XLanguage> GameInfoDatabase::GetSupportedLanguages() const {
  if (!xlast_gamedata_) {
    return {};
  }

  return xlast_gamedata_->GetSupportedLanguages();
}

GameInfoDatabase::ProductInformation GameInfoDatabase::GetProductInformation()
    const {
  if (!xlast_gamedata_) {
    return {};
  }

  ProductInformation info = {};

  const auto attributes = xlast_gamedata_->GetProductInformationAttributes();
  for (const auto& attribute : attributes) {
    switch (attribute.first) {
      case ProductInformationEntry::MaxOfflinePlayers:
        info.max_offline_players_count = attribute.second;
        break;
      case ProductInformationEntry::MaxSystemLinkPlayers:
        info.max_systemlink_players_count = attribute.second;
        break;
      case ProductInformationEntry::MaxLivePlayers:
        info.max_live_players_count = attribute.second;
        break;
      case ProductInformationEntry::PublisherString:
        info.publisher_name = xe::to_utf8(xlast_gamedata_->GetLocalizedString(
            attribute.second, XLanguage::kEnglish));
        break;
      case ProductInformationEntry::DeveloperString:
        info.developer_name = xe::to_utf8(xlast_gamedata_->GetLocalizedString(
            attribute.second, XLanguage::kEnglish));
        break;
      case ProductInformationEntry::MarketingString:
        info.marketing_info = xe::to_utf8(xlast_gamedata_->GetLocalizedString(
            attribute.second, XLanguage::kEnglish));
        break;
      case ProductInformationEntry::GenreTypeString:
        info.genre_description =
            xe::to_utf8(xlast_gamedata_->GetLocalizedString(
                attribute.second, XLanguage::kEnglish));
        break;
      default:
        break;
    }
  }
  return info;
}

// Aggregators
std::vector<GameInfoDatabase::Context> GameInfoDatabase::GetContexts() const {
  std::vector<Context> contexts;

  if (!is_valid_) {
    return contexts;
  }

  const auto xdbf_contexts = xdbf_gamedata_->GetContexts();
  for (const auto& entry : xdbf_contexts) {
    contexts.push_back(GetContext(entry.id));
  }

  return contexts;
}

std::vector<GameInfoDatabase::Property> GameInfoDatabase::GetProperties()
    const {
  std::vector<Property> properties;

  if (!is_valid_) {
    return properties;
  }

  const auto xdbf_properties = xdbf_gamedata_->GetProperties();
  for (const auto& entry : xdbf_properties) {
    properties.push_back(GetProperty(entry.id));
  }

  return properties;
}

std::vector<GameInfoDatabase::Achievement> GameInfoDatabase::GetAchievements()
    const {
  std::vector<Achievement> achievements;

  if (!is_valid_) {
    return achievements;
  }

  const auto xdbf_achievements = xdbf_gamedata_->GetAchievements();
  for (const auto& entry : xdbf_achievements) {
    achievements.push_back(GetAchievement(entry.id));
  }

  return achievements;
}

}  // namespace util
}  // namespace kernel
}  // namespace xe