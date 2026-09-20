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

#include "pb2025_sentry_behavior/plugins/action/print_referee_status.hpp"

namespace pb2025_sentry_behavior
{

PrintRefereeStatusAction::PrintRefereeStatusAction(
  const std::string & name, const BT::NodeConfig & config)
: BT::SyncActionNode(name, config)
{
}

BT::PortsList PrintRefereeStatusAction::providedPorts()
{
  return {
    // /referee/common/*
    BT::InputPort<dji_referee_protocol::msg::GameStatus>(
      "game_status", "{@referee_gameStatus}", "GameStatus"),
    BT::InputPort<dji_referee_protocol::msg::RobotHP>(
      "robot_hp", "{@referee_robotHP}", "RobotHP"),
    BT::InputPort<dji_referee_protocol::msg::RobotPerformance>(
      "robot_performance", "{@referee_robotPerformance}", "RobotPerformance"),
    BT::InputPort<dji_referee_protocol::msg::RobotHeat>(
      "robot_heat", "{@referee_robotHeat}", "RobotHeat"),
    BT::InputPort<dji_referee_protocol::msg::RobotPosition>(
      "robot_position", "{@referee_robotPosition}", "RobotPosition"),
    BT::InputPort<dji_referee_protocol::msg::AllowedShoot>(
      "allowed_shoot", "{@referee_allowedShoot}", "AllowedShoot"),
    BT::InputPort<dji_referee_protocol::msg::RFIDStatus>(
      "rfid_status", "{@referee_rfidStatus}", "RFIDStatus"),
    BT::InputPort<dji_referee_protocol::msg::FieldEvent>(
      "field_event", "{@referee_fieldEvent}", "FieldEvent"),
    BT::InputPort<dji_referee_protocol::msg::RobotBuff>(
      "robot_buff", "{@referee_robotBuff}", "RobotBuff"),
    BT::InputPort<dji_referee_protocol::msg::GroundRobotPosition>(
      "ground_robot_position", "{@referee_groundRobotPosition}", "GroundRobotPosition"),
    BT::InputPort<dji_referee_protocol::msg::DamageState>(
      "damage_state", "{@referee_damageState}", "DamageState"),
    // /referee/parsed/common/*
    BT::InputPort<dji_referee_protocol::msg::Constraints>(
      "constraints", "{@referee_constraints}", "Constraints"),
    BT::InputPort<dji_referee_protocol::msg::SelfColor>(
      "self_color", "{@referee_selfColor}", "SelfColor"),
  };
}

BT::NodeStatus PrintRefereeStatusAction::tick()
{
  int received = 0;
  int total = 13;

  RCLCPP_INFO(logger_, "======== Sentry Referee Status ========");

  // --- SelfColor ---
  if (auto m = getInput<dji_referee_protocol::msg::SelfColor>("self_color")) {
    const char * color_str = m->color == 1 ? "RED" : (m->color == 2 ? "BLUE" : "UNKNOWN");
    RCLCPP_INFO(logger_, "[SelfColor] color=%s(%d)", color_str, m->color);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[SelfColor] NOT received");
  }

  // --- GameStatus ---
  if (auto m = getInput<dji_referee_protocol::msg::GameStatus>("game_status")) {
    RCLCPP_INFO(
      logger_, "[GameStatus] type=%d, progress=%d, remain_time=%ds",
      m->game_type, m->game_progress, m->stage_remain_time);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[GameStatus] NOT received");
  }

  // --- RobotPerformance (self) ---
  if (auto m = getInput<dji_referee_protocol::msg::RobotPerformance>("robot_performance")) {
    RCLCPP_INFO(
      logger_, "[RobotPerformance] id=%d, lv=%d, hp=%d/%d, heat_limit=%d, power_limit=%d",
      m->robot_id, m->robot_level, m->current_hp, m->maximum_hp,
      m->shooter_barrel_heat_limit, m->chassis_power_limit);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RobotPerformance] NOT received");
  }

  // --- RobotHeat (self) ---
  if (auto m = getInput<dji_referee_protocol::msg::RobotHeat>("robot_heat")) {
    RCLCPP_INFO(
      logger_, "[RobotHeat] 17mm_heat=%d, 42mm_heat=%d, buffer=%d, power=%.1f",
      m->shooter_17mm_barrel_heat, m->shooter_42mm_barrel_heat,
      m->buffer_energy, m->chassis_current_power);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RobotHeat] NOT received");
  }

  // --- AllowedShoot ---
  if (auto m = getInput<dji_referee_protocol::msg::AllowedShoot>("allowed_shoot")) {
    RCLCPP_INFO(
      logger_, "[AllowedShoot] 17mm=%d, 42mm=%d, gold=%d",
      m->projectile_allowance_17mm, m->projectile_allowance_42mm, m->remaining_gold_coin);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[AllowedShoot] NOT received");
  }

  // --- Constraints (parsed) ---
  if (auto m = getInput<dji_referee_protocol::msg::Constraints>("constraints")) {
    RCLCPP_INFO(
      logger_, "[Constraints] heat=%.1f/%.1f, power=%.1f/%.1f, fire=%s, speed_scale=%.2f",
      m->shooter_heat, m->heat_limit, m->chassis_power, m->chassis_power_limit,
      m->fire_allowed ? "YES" : "NO", m->speed_scale);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[Constraints] NOT received");
  }

  // --- RobotHP (sentry=7, outpost, base) ---
  if (auto m = getInput<dji_referee_protocol::msg::RobotHP>("robot_hp")) {
    RCLCPP_INFO(
      logger_,
      "[RobotHP] RED: sentry=%d, outpost=%d, base=%d | BLUE: sentry=%d, outpost=%d, base=%d",
      m->red_7_robot_hp, m->red_outpost_hp, m->red_base_hp,
      m->blue_7_robot_hp, m->blue_outpost_hp, m->blue_base_hp);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RobotHP] NOT received");
  }

  // --- RobotPosition (self) ---
  if (auto m = getInput<dji_referee_protocol::msg::RobotPosition>("robot_position")) {
    RCLCPP_INFO(
      logger_, "[RobotPosition] x=%.2f, y=%.2f, angle=%.1f",
      m->x, m->y, m->angle);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RobotPosition] NOT received");
  }

  // --- GroundRobotPosition (teammates) ---
  if (auto m = getInput<dji_referee_protocol::msg::GroundRobotPosition>("ground_robot_position")) {
    RCLCPP_INFO(
      logger_, "[GroundRobotPos] hero=(%.1f,%.1f), eng=(%.1f,%.1f), inf3=(%.1f,%.1f), inf4=(%.1f,%.1f)",
      m->hero_x, m->hero_y, m->engineer_x, m->engineer_y,
      m->infantry_3_x, m->infantry_3_y, m->infantry_4_x, m->infantry_4_y);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[GroundRobotPos] NOT received");
  }

  // --- RobotBuff ---
  if (auto m = getInput<dji_referee_protocol::msg::RobotBuff>("robot_buff")) {
    RCLCPP_INFO(
      logger_, "[RobotBuff] recovery=%d%%, cooling=%dx, defence=%d%%, attack=%d%%",
      m->recovery_buff, m->cooling_buff, m->defence_buff, m->attack_buff);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RobotBuff] NOT received");
  }

  // --- RFIDStatus ---
  if (auto m = getInput<dji_referee_protocol::msg::RFIDStatus>("rfid_status")) {
    RCLCPP_INFO(
      logger_, "[RFIDStatus] bits_low=0x%08X, bits_high=0x%02X",
      m->detected_rfid_bits_low, m->detected_rfid_bits_high);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[RFIDStatus] NOT received");
  }

  // --- FieldEvent ---
  if (auto m = getInput<dji_referee_protocol::msg::FieldEvent>("field_event")) {
    RCLCPP_INFO(
      logger_, "[FieldEvent] fortress_buff=%d, outpost_buff=%d, base_buff=%d",
      m->fortress_buff_point, m->outpost_buff_point, m->base_buff_point ? 1 : 0);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[FieldEvent] NOT received");
  }

  // --- DamageState ---
  if (auto m = getInput<dji_referee_protocol::msg::DamageState>("damage_state")) {
    RCLCPP_INFO(
      logger_, "[DamageState] armor_id=%d, type=%d",
      m->armor_id, m->damage_type);
    ++received;
  } else {
    RCLCPP_WARN(logger_, "[DamageState] NOT received");
  }

  RCLCPP_INFO(logger_, "======== %d/%d topics received ========", received, total);
  return BT::NodeStatus::SUCCESS;
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_cpp/bt_factory.h"
BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<pb2025_sentry_behavior::PrintRefereeStatusAction>("PrintRefereeStatus");
}
