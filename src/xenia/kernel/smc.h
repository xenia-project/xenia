/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SMC_H_
#define XENIA_KERNEL_SMC_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>

enum X_DVD_DISC_STATE {
  NO_DISC,
  XBOX_360_GAME_DISC,
  XBOX_GAME_DISC,
  UNKNOWN_DISC,
  MOVIE_DVD,
  MOVIE_DVD2,
  MOVIE_DVD3,
  CD,
  MIXED_MEDIA_DISC,
  UNKNOWN_DISC2,
};

enum class X_DVD_TRAY_STATE : uint8_t {
  OPEN,
  UNKNOWN,
  CLOSED,
  OPENING,
  CLOSING
};

enum REMOTE_CONTROL : uint8_t {
  MEDIA_REMOTE_BOTH = 0x01,
  MEDIA_REMOTE_360 = 0x0F
};

enum TILT_STATE : uint8_t { VERTICAL, HORIZONTAL };

enum POWER_LED_STATE : uint8_t {
  POWER_ON = 0x01,
  POWER_DEFAULT = 0x02,
  POWER_OFF = 0x03,
  POWER_BLINK = 0x10
};

enum LED_STATE : uint8_t {
  OFF = 0x00,
  RED = 0x08,
  GREEN = 0x80,
  ORANGE = 0x88
};

// https://free60.org/Hardware/Console/SMC/
enum X_SMC_CMD : uint8_t {
  POWERON_STATE = 0x1,
  QUERY_RTC = 0x4,
  QUERY_TEMP_SENSOR = 0x7,
  QUERY_TRAY = 0xA,
  QUERY_AV_PACK = 0xF,
  I2C_READ_WRITE = 0x11,
  QUERY_SMC_VERSION = 0x12,
  FIFO_TEST = 0x13,
  QUERY_IR_ADDRESS = 0x16,
  QUERY_TILT_SENSOR = 0x17,
  READ_82_INTERRUPTS = 0x1E,
  READ_8E_INTERRUPTS = 0x20,
  SET_STANDBY = 0x82,
  SET_TIME = 0x85,
  SET_FAN_ALGORITHM = 0x88,
  SET_FAN_SPEED_CPU = 0x89,
  SET_DVD_TRAY = 0x8B,
  SET_POWER_LED = 0x8C,
  SET_AUDIO_MUTE = 0x8D,
  ARGON_RELATED = 0x90,
  SET_FAN_SPEED_GPU =
      0x94,  // not present on slim, not used/respected on newer fat
  SET_IR_ADDRESS = 0x95,
  SET_DVD_TRAY_SECURE = 0x98,
  SET_LEDS = 0x99,
  SET_RTC_WAKE = 0x9A,
  ANA_RELATED = 0x9B,
  SET_ASYNC_OPERATION = 0x9C,
  SET_82_INTERRUPTS = 0x9D,
  SET_9F_INTERRUPTS = 0x9F,
};

struct X_TEMPERATURE_DATA {
  uint8_t integer_value_;
  uint8_t fractional_value_;

  void SetTemp(float celsius_temp) {
    const uint16_t value = static_cast<uint16_t>(celsius_temp * 256.0f);

    integer_value_ = value & 0xFF;
    fractional_value_ = value >> 8;
  }
};

struct X_SMC_DATA {
  X_SMC_CMD command;

  union {
    uint8_t smc_data[15];

    // QUERY_TEMP_SENSOR
    struct {
      X_TEMPERATURE_DATA cpu;
      X_TEMPERATURE_DATA gpu;
      X_TEMPERATURE_DATA edram;
      X_TEMPERATURE_DATA mb;
    } temps;

    // QUERY_TRAY
    struct {
      X_DVD_TRAY_STATE state;
    } dvd_tray;

    // QUERY_AV_PACK
    struct {
      uint8_t av_pack;
    } av_pack;

    // QUERY_SMC_VERSION
    struct {
      uint8_t unk;
      uint8_t major;
      uint8_t minor;
    } smc_version;

    // QUERY_IR_ADDRESS
    struct {
      REMOTE_CONTROL ir_address;
    } ir_address;

    // QUERY_TILT_STATE
    struct {
      TILT_STATE tilt_state;
    } tilt_state;

    // POWER_LED_STATE
    struct {
      POWER_LED_STATE state;
      uint8_t animate;
    } power_led_state;

    // LED_STATE
    struct {
      LED_STATE state;
      uint8_t region;  // top_left >> 3 | top_right >> 2 | bottom_left >> 1 |
                       // bottom_right;
    } led_state;
  };
};

namespace xe {
namespace kernel {

// SMC = System Management Controller
// https://github.com/landaire/LaunchCode/blob/master/LaunchCode/smc.cpp
// https://github.com/XboxUnity/freestyledash/blob/master/Freestyle/Tools/SMC/smc.cpp
class SystemManagementController {
 public:
  SystemManagementController();
  ~SystemManagementController();

  std::array<uint8_t, 3> GetSMCVersion() const { return smc_version; }
  X_DVD_TRAY_STATE GetTrayState() const { return dvd_tray_state_; }

  void SetTrayState(X_DVD_TRAY_STATE state);

  void CallCommand(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);

 private:
  const std::array<uint8_t, 3> smc_version = {65, 2, 1};
  const std::map<uint8_t, uint8_t> av_pack_to_smc_value = {
      {0, 0x43}, {3, 0x4F}, {4, 0x13}, {5, 0x0F}, {6, 0x5B}, {8, 0x1F}};

  void QueryTemperatureSensor(X_SMC_DATA* smc_message,
                              X_SMC_DATA* smc_response);
  void QueryDriveTraySensor(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void QueryAvPack(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void QuerySmcVersion(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void QueryIRAddress(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void QueryTiltState(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void SetDriveTray(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void SetFanSpeed(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void SetIRAddress(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void SetPowerLed(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);
  void SetLedState(X_SMC_DATA* smc_message, X_SMC_DATA* smc_response);

  std::map<X_SMC_CMD, std::function<void(X_SMC_DATA* smc_message,
                                         X_SMC_DATA* smc_response)>>
      smc_commands_;

  X_DVD_TRAY_STATE dvd_tray_state_ = X_DVD_TRAY_STATE::OPEN;
  REMOTE_CONTROL ir_address_ = REMOTE_CONTROL::MEDIA_REMOTE_360;
  TILT_STATE tilt_state_ = TILT_STATE::VERTICAL;

  uint32_t cpu_fan_speed_ = 100;
  uint32_t gpu_fan_speed_ = 100;

  std::pair<POWER_LED_STATE, uint8_t> power_led_state_ = {
      POWER_LED_STATE::POWER_ON, 0x00};

  std::pair<LED_STATE, uint8_t> led_state_ = {LED_STATE::GREEN, 0x01};
};

}  // namespace kernel
}  // namespace xe

#endif
