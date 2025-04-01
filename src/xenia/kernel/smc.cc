/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/smc.h"
#include "xenia/kernel/util/shim_utils.h"

DECLARE_int32(avpack);

namespace xe {
namespace kernel {

SystemManagementController::SystemManagementController()
    : dvd_tray_state_(X_DVD_TRAY_STATE::OPEN) {
  smc_commands_[X_SMC_CMD::QUERY_TEMP_SENSOR] =
      std::bind(&SystemManagementController::QueryTemperatureSensor, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::QUERY_TRAY] =
      std::bind(&SystemManagementController::QueryDriveTraySensor, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::QUERY_AV_PACK] =
      std::bind(&SystemManagementController::QueryAvPack, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::QUERY_SMC_VERSION] =
      std::bind(&SystemManagementController::QuerySmcVersion, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::QUERY_IR_ADDRESS] =
      std::bind(&SystemManagementController::QueryIRAddress, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::QUERY_TILT_SENSOR] =
      std::bind(&SystemManagementController::QueryTiltState, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_FAN_SPEED_CPU] =
      std::bind(&SystemManagementController::SetFanSpeed, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_FAN_SPEED_GPU] =
      std::bind(&SystemManagementController::SetFanSpeed, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_DVD_TRAY] =
      std::bind(&SystemManagementController::SetDriveTray, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_IR_ADDRESS] =
      std::bind(&SystemManagementController::SetIRAddress, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_POWER_LED] =
      std::bind(&SystemManagementController::SetPowerLed, this,
                std::placeholders::_1, std::placeholders::_2);
  smc_commands_[X_SMC_CMD::SET_LEDS] =
      std::bind(&SystemManagementController::SetLedState, this,
                std::placeholders::_1, std::placeholders::_2);
};
SystemManagementController::~SystemManagementController() {};

void SystemManagementController::SetTrayState(X_DVD_TRAY_STATE state) {
  dvd_tray_state_ = state;
  kernel_state()->BroadcastNotification(kXNotificationDvdDriveTrayStateChanged,
                                        static_cast<uint8_t>(state));
}

void SystemManagementController::CallCommand(X_SMC_DATA* smc_message,
                                             X_SMC_DATA* smc_response) {
  const auto itr = smc_commands_.find(smc_message->command);
  if (itr == smc_commands_.cend()) {
    XELOGW("Unimplemented SMC Command: {:02X}",
           static_cast<uint8_t>(smc_message->command));
    return;
  }

  itr->second(smc_message, smc_response);
}

void SystemManagementController::QueryTemperatureSensor(
    X_SMC_DATA* smc_message, X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->temps.cpu.SetTemp(69.6f);
  smc_response->temps.gpu.SetTemp(69.9f);
  smc_response->temps.edram.SetTemp(69.6f);
  smc_response->temps.mb.SetTemp(69.9f);
}

void SystemManagementController::QueryDriveTraySensor(
    X_SMC_DATA* smc_message, X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->dvd_tray.state = dvd_tray_state_;
};

void SystemManagementController::QueryAvPack(X_SMC_DATA* smc_message,
                                             X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->av_pack.av_pack = 0;

  const auto entry = av_pack_to_smc_value.find(cvars::avpack);
  if (entry == av_pack_to_smc_value.cend()) {
    return;
  }

  smc_response->av_pack.av_pack = entry->second;
}

void SystemManagementController::QuerySmcVersion(X_SMC_DATA* smc_message,
                                                 X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->smc_version.unk = smc_version[0];
  smc_response->smc_version.major = smc_version[1];
  smc_response->smc_version.minor = smc_version[2];
}

void SystemManagementController::QueryIRAddress(X_SMC_DATA* smc_message,
                                                X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->ir_address.ir_address = ir_address_;
}

void SystemManagementController::QueryTiltState(X_SMC_DATA* smc_message,
                                                X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->tilt_state.tilt_state = tilt_state_;
}

void SystemManagementController::SetIRAddress(X_SMC_DATA* smc_message,
                                              X_SMC_DATA* smc_response) {
  if (!smc_response) {
    return;
  }

  smc_response->command = smc_message->command;
  smc_response->ir_address.ir_address = ir_address_;
}

void SystemManagementController::SetDriveTray(X_SMC_DATA* smc_message,
                                              X_SMC_DATA* smc_response) {
  SetTrayState(
      static_cast<X_DVD_TRAY_STATE>((smc_message->smc_data[0] & 0xF) % 5));
}

void SystemManagementController::SetFanSpeed(X_SMC_DATA* smc_message,
                                             X_SMC_DATA* smc_response) {
  if (smc_message->command == X_SMC_CMD::SET_FAN_SPEED_CPU) {
    cpu_fan_speed_ = (smc_message->smc_data[0] - 0x80);
  }
  if (smc_message->command == X_SMC_CMD::SET_FAN_SPEED_GPU) {
    gpu_fan_speed_ = (smc_message->smc_data[0] - 0x80);
  }
}

void SystemManagementController::SetPowerLed(X_SMC_DATA* smc_message,
                                             X_SMC_DATA* smc_response) {
  power_led_state_ = {smc_message->power_led_state.state,
                      smc_message->power_led_state.animate};
}

void SystemManagementController::SetLedState(X_SMC_DATA* smc_message,
                                             X_SMC_DATA* smc_response) {
  led_state_ = {smc_message->led_state.state, smc_message->led_state.region};
}

}  // namespace kernel
}  // namespace xe