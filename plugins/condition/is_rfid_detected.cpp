// Copyright 2025 Lihan Chen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pb2025_sentry_behavior/plugins/condition/is_rfid_detected.hpp"

#include "dji_referee_protocol/msg/rfid_status.hpp"

namespace pb2025_sentry_behavior
{

namespace
{
// Decode an RFID bit from the raw status field.
inline bool rfidBit(uint32_t bits_low, uint8_t bits_high, int index)
{
  if (index < 32) return (bits_low >> index) & 1;
  return (bits_high >> (index - 32)) & 1;
}
}  // namespace

IsRfidDetectedCondition::IsRfidDetectedCondition(
  const std::string & name, const BT::NodeConfig & config)
: BT::SimpleConditionNode(name, std::bind(&IsRfidDetectedCondition::checkRfidStatus, this), config)
{
}

BT::NodeStatus IsRfidDetectedCondition::checkRfidStatus()
{
  bool friendly_fortress_gain_point, friendly_supply_zone_non_exchange,
    friendly_supply_zone_exchange, center_gain_point;
  auto msg = getInput<dji_referee_protocol::msg::RFIDStatus>("key_port");
  if (!msg) {
    RCLCPP_ERROR(logger_, "RFIDStatus message is not available");
    return BT::NodeStatus::FAILURE;
  }

  // Decode the configured RFID status bits.
  uint32_t bits_low = msg->detected_rfid_bits_low;
  uint8_t bits_high = msg->detected_rfid_bits_high;

  // bit 17: friendly_fortress_gain_point
  // bit 19: friendly_supply_zone_non_exchange
  // bit 20: friendly_supply_zone_exchange
  // bit 23: center_gain_point

  bool fortress_detected = rfidBit(bits_low, bits_high, 17);
  bool supply_non_exchange_detected = rfidBit(bits_low, bits_high, 19);
  bool supply_exchange_detected = rfidBit(bits_low, bits_high, 20);
  bool center_detected = rfidBit(bits_low, bits_high, 23);

  getInput("friendly_fortress_gain_point", friendly_fortress_gain_point);
  getInput("friendly_supply_zone_non_exchange", friendly_supply_zone_non_exchange);
  getInput("friendly_supply_zone_exchange", friendly_supply_zone_exchange);
  getInput("center_gain_point", center_gain_point);

  if (
    (friendly_fortress_gain_point && fortress_detected) ||
    (friendly_supply_zone_non_exchange && supply_non_exchange_detected) ||
    (friendly_supply_zone_exchange && supply_exchange_detected) ||
    (center_gain_point && center_detected)) {
    return BT::NodeStatus::SUCCESS;
  } else {
    return BT::NodeStatus::FAILURE;
  }
}

BT::PortsList IsRfidDetectedCondition::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::RFIDStatus>(
      "key_port", "{@referee_rfidStatus}", "RFIDStatus port on blackboard"),
    BT::InputPort<bool>("friendly_fortress_gain_point", false, "己方堡垒增益点"),
    BT::InputPort<bool>(
      "friendly_supply_zone_non_exchange", false, "己方与兑换区不重叠的补给区 / RMUL 补给区"),
    BT::InputPort<bool>("friendly_supply_zone_exchange", false, "己方与兑换区重叠的补给区"),
    BT::InputPort<bool>("center_gain_point", false, "中心增益点（仅 RMUL 适用）"),
  };
}
}  // namespace pb2025_sentry_behavior

#include "behaviortree_cpp/bt_factory.h"
BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<pb2025_sentry_behavior::IsRfidDetectedCondition>("IsRfidDetected");
}
