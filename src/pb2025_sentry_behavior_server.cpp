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

#include "pb2025_sentry_behavior/pb2025_sentry_behavior_server.hpp"

#include <filesystem>
#include <fstream>

#include "auto_aim_interfaces/msg/armors.hpp"
#include "auto_aim_interfaces/msg/target.hpp"
#include "behaviortree_cpp/xml_parsing.h"
#include "dji_referee_protocol/msg/allowed_shoot.hpp"
#include "dji_referee_protocol/msg/damage_state.hpp"
#include "dji_referee_protocol/msg/field_event.hpp"
#include "dji_referee_protocol/msg/game_status.hpp"
#include "dji_referee_protocol/msg/ground_robot_position.hpp"
#include "dji_referee_protocol/msg/rfid_status.hpp"
#include "dji_referee_protocol/msg/robot_buff.hpp"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_hp.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"
#include "dji_referee_protocol/msg/constraints.hpp"
#include "dji_referee_protocol/msg/robot_position.hpp"
#include "dji_referee_protocol/msg/self_color.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "std_msgs/msg/int32.hpp"
namespace pb2025_sentry_behavior
{

template <typename T>
void SentryBehaviorServer::subscribe(
  const std::string & topic, const std::string & bb_key, const rclcpp::QoS & qos)
{
  auto sub = node()->create_subscription<T>(
    topic, qos,
    [this, bb_key](const typename T::SharedPtr msg) { globalBlackboard()->set(bb_key, *msg); });
  subscriptions_.push_back(sub);
}

SentryBehaviorServer::SentryBehaviorServer(const rclcpp::NodeOptions & options)
: TreeExecutionServer(options)
{
  node()->declare_parameter("use_cout_logger", false);
  node()->get_parameter("use_cout_logger", use_cout_logger_);

  // Direct DJI referee protocol subscriptions (no translator)
  subscribe<dji_referee_protocol::msg::GameStatus>(
    "/referee/common/game_status", "referee_gameStatus");
  subscribe<dji_referee_protocol::msg::RobotHP>(
    "/referee/common/robot_hp", "referee_robotHP");
  subscribe<dji_referee_protocol::msg::RobotPerformance>(
    "/referee/common/robot_performance", "referee_robotPerformance");
  subscribe<dji_referee_protocol::msg::RobotHeat>(
    "/referee/common/robot_heat", "referee_robotHeat");
  subscribe<dji_referee_protocol::msg::RobotPosition>(
    "/referee/common/robot_position", "referee_robotPosition");
  subscribe<dji_referee_protocol::msg::AllowedShoot>(
    "/referee/common/allowed_shoot", "referee_allowedShoot");
  subscribe<dji_referee_protocol::msg::RFIDStatus>(
    "/referee/common/rfid_status", "referee_rfidStatus");
  subscribe<dji_referee_protocol::msg::FieldEvent>(
    "/referee/common/field_event", "referee_fieldEvent");
  subscribe<dji_referee_protocol::msg::RobotBuff>(
    "/referee/common/robot_buff", "referee_robotBuff");
  subscribe<dji_referee_protocol::msg::GroundRobotPosition>(
    "/referee/common/ground_robot_position", "referee_groundRobotPosition");

  // Parsed referee topics
  subscribe<dji_referee_protocol::msg::Constraints>(
    "/referee/parsed/common/constraints", "referee_constraints");
  subscribe<dji_referee_protocol::msg::SelfColor>(
    "/referee/parsed/common/self_color", "referee_selfColor");

  // DamageState: custom callback with latch (visible for exactly one BT tick)
  auto damage_sub = node()->create_subscription<dji_referee_protocol::msg::DamageState>(
    "/referee/common/damage_state", rclcpp::QoS(10),
    [this](const dji_referee_protocol::msg::DamageState::SharedPtr msg) {
      std::lock_guard<std::mutex> lock(damage_mutex_);
      globalBlackboard()->set("referee_damageState", *msg);
      damage_on_bb_ = true;
    });
  subscriptions_.push_back(damage_sub);

  subscribe<std_msgs::msg::Int32>("manual_start", "manual_start");

  auto detector_qos = rclcpp::SensorDataQoS();
  subscribe<auto_aim_interfaces::msg::Armors>("detector/armors", "detector_armors", detector_qos);
  auto tracker_qos = rclcpp::SensorDataQoS();
  subscribe<auto_aim_interfaces::msg::Target>("tracker/target", "tracker_target", tracker_qos);

  auto costmap_qos = rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable();
  subscribe<nav_msgs::msg::OccupancyGrid>(
    "global_costmap/costmap", "nav_globalCostmap", costmap_qos);

}

bool SentryBehaviorServer::onGoalReceived(
  const std::string & tree_name, const std::string & payload)
{
  RCLCPP_INFO(
    node()->get_logger(), "onGoalReceived with tree name '%s' with payload '%s'", tree_name.c_str(),
    payload.c_str());
  return true;
}

void SentryBehaviorServer::onTreeCreated(BT::Tree & tree)
{
  if (use_cout_logger_) {
    logger_cout_ = std::make_shared<BT::StdCoutLogger>(tree);
  }
  tick_count_ = 0;
}

std::optional<BT::NodeStatus> SentryBehaviorServer::onLoopAfterTick(BT::NodeStatus /*status*/)
{
  ++tick_count_;
  // Clear damage latch: damage was visible for one BT tick, now reset
  {
    std::lock_guard<std::mutex> lock(damage_mutex_);
    if (damage_on_bb_) {
      globalBlackboard()->set("referee_damageState", dji_referee_protocol::msg::DamageState());
      damage_on_bb_ = false;
    }
  }
  return std::nullopt;
}

std::optional<std::string> SentryBehaviorServer::onTreeExecutionCompleted(
  BT::NodeStatus status, bool was_cancelled)
{
  RCLCPP_INFO(
    node()->get_logger(), "onTreeExecutionCompleted with status=%d (canceled=%d) after %d ticks",
    static_cast<int>(status), was_cancelled, tick_count_);
  logger_cout_.reset();
  std::string result = treeName() +
                       " tree completed with status=" + std::to_string(static_cast<int>(status)) +
                       " after " + std::to_string(tick_count_) + " ticks";
  return result;
}

}  // namespace pb2025_sentry_behavior

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  rclcpp::NodeOptions options;
  auto action_server = std::make_shared<pb2025_sentry_behavior::SentryBehaviorServer>(options);

  RCLCPP_INFO(action_server->node()->get_logger(), "Starting SentryBehaviorServer");

  rclcpp::executors::MultiThreadedExecutor exec(
    rclcpp::ExecutorOptions(), 0, false, std::chrono::milliseconds(250));
  exec.add_node(action_server->node());
  exec.spin();
  exec.remove_node(action_server->node());

  // Groot2 editor requires a model of your registered Nodes.
  // You don't need to write that by hand, it can be automatically
  // generated using the following command.
  std::string xml_models = BT::writeTreeNodesModelXML(action_server->factory());

  // Save the XML models to a file
  std::ofstream file(std::filesystem::path(ROOT_DIR) / "behavior_trees" / "models.xml");
  file << xml_models;

  rclcpp::shutdown();
}
