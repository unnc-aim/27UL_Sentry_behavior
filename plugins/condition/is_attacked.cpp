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

#include "pb2025_sentry_behavior/plugins/condition/is_attacked.hpp"

#include "dji_referee_protocol/msg/damage_state.hpp"

namespace pb2025_sentry_behavior
{

IsAttackedCondition::IsAttackedCondition(const std::string & name, const BT::NodeConfig & config)
: BT::SimpleConditionNode(name, std::bind(&IsAttackedCondition::checkIsAttacked, this), config)
{
}

BT::NodeStatus IsAttackedCondition::checkIsAttacked()
{
  auto msg = getInput<dji_referee_protocol::msg::DamageState>("damage_port");
  if (!msg) {
    RCLCPP_ERROR(logger_, "DamageState message is not available");
    return BT::NodeStatus::FAILURE;
  }

  // DamageState is latched: non-zero damage_type means damage event occurred
  const bool is_attacked = (msg->damage_type != 0);

  if (is_attacked) {
    RCLCPP_DEBUG(logger_, "Armor hit detected");
    float hit_armor_pose = 0.0;
    switch (msg->armor_id) {
      // Anticlockwise from armor 0
      case 0:
        hit_armor_pose = 0.0;
        break;
      case 1:
        hit_armor_pose = M_PI_2;
        break;
      case 2:
        hit_armor_pose = M_PI;
        break;
      case 3:
        hit_armor_pose = -M_PI_2;
        break;
      default:
        RCLCPP_WARN(logger_, "Invalid armor id: %d", msg->armor_id);
        break;
    }
    setOutput("gimbal_pitch", 0.0);
    setOutput("gimbal_yaw", hit_armor_pose);
  }

  return is_attacked ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}

BT::PortsList IsAttackedCondition::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::DamageState>(
      "damage_port", "{@referee_damageState}", "DamageState port on blackboard"),
    BT::OutputPort<float>(
      "gimbal_pitch", "{gimbal_pitch}",
      "Move gimbal_pitch (const 0.0) to the direction of the hit armor plate"),
    BT::OutputPort<float>(
      "gimbal_yaw", "{gimbal_yaw}", "Move gimbal_yaw to the direction of the hit armor plate")};
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_cpp/bt_factory.h"
BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<pb2025_sentry_behavior::IsAttackedCondition>("IsAttacked");
}
