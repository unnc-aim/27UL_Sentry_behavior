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

#include "pb2025_sentry_behavior/plugins/condition/is_status_ok.hpp"

#include "dji_referee_protocol/msg/allowed_shoot.hpp"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"

namespace pb2025_sentry_behavior
{

IsStatusOKCondition::IsStatusOKCondition(
  const std::string & name, const BT::NodeConfig & config)
: BT::SimpleConditionNode(name, std::bind(&IsStatusOKCondition::checkRobotStatus, this), config)
{
}

BT::NodeStatus IsStatusOKCondition::checkRobotStatus()
{
  int hp_min, heat_max, ammo_min;

  auto hp_msg = getInput<dji_referee_protocol::msg::RobotPerformance>("hp_port");
  auto heat_msg = getInput<dji_referee_protocol::msg::RobotHeat>("heat_port");
  auto ammo_msg = getInput<dji_referee_protocol::msg::AllowedShoot>("ammo_port");

  if (!hp_msg) {
    RCLCPP_ERROR(logger_, "RobotPerformance message is not available");
    return BT::NodeStatus::FAILURE;
  }
  if (!heat_msg) {
    RCLCPP_ERROR(logger_, "RobotHeat message is not available");
    return BT::NodeStatus::FAILURE;
  }
  if (!ammo_msg) {
    RCLCPP_ERROR(logger_, "AllowedShoot message is not available");
    return BT::NodeStatus::FAILURE;
  }

  getInput("hp_min", hp_min);
  getInput("heat_max", heat_max);
  getInput("ammo_min", ammo_min);

  const bool is_hp_ok = (hp_msg->current_hp >= hp_min);
  const bool is_heat_ok = (heat_msg->shooter_17mm_barrel_heat <= heat_max);
  const bool is_ammo_ok = (ammo_msg->projectile_allowance_17mm >= ammo_min);

  return (is_hp_ok && is_heat_ok && is_ammo_ok) ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}

BT::PortsList IsStatusOKCondition::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::RobotPerformance>(
      "hp_port", "{@referee_robotPerformance}", "RobotPerformance for HP check"),
    BT::InputPort<dji_referee_protocol::msg::RobotHeat>(
      "heat_port", "{@referee_robotHeat}", "RobotHeat for heat check"),
    BT::InputPort<dji_referee_protocol::msg::AllowedShoot>(
      "ammo_port", "{@referee_allowedShoot}", "AllowedShoot for ammo check"),
    BT::InputPort<int>("hp_min", 300, "Minimum HP. NOTE: Sentry init/max HP is 400"),
    BT::InputPort<int>("heat_max", 350, "Maximum heat. NOTE: Sentry heat limit is 400"),
    BT::InputPort<int>("ammo_min", 0, "Lower then minimum ammo will return FAILURE"),
  };
}
}  // namespace pb2025_sentry_behavior

#include "behaviortree_cpp/bt_factory.h"
BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<pb2025_sentry_behavior::IsStatusOKCondition>("IsStatusOK");
}
