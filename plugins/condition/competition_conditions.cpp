#include "pb2025_sentry_behavior/plugins/condition/competition_conditions.hpp"

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"
#include "rclcpp/rclcpp.hpp"

namespace pb2025_sentry_behavior
{

IsHpLow::IsHpLow(const std::string & name, const BT::NodeConfig & config)
: BT::ConditionNode(name, config)
{
}

BT::PortsList IsHpLow::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::RobotPerformance>(
      "hp_port", "{@referee_robotPerformance}", "Self robot performance message"),
    BT::InputPort<int>("threshold", 150, "Inclusive low HP threshold"),
    BT::OutputPort<bool>("hp_low", "Result for diagnostics"),
  };
}

BT::NodeStatus IsHpLow::tick()
{
  auto threshold = getInput<int>("threshold");
  if (!threshold || threshold.value() < 0 || threshold.value() > 65535) {
    throw BT::RuntimeError("IsHpLow: threshold must be in [0, 65535]");
  }
  const auto msg = getInput<dji_referee_protocol::msg::RobotPerformance>("hp_port");
  // Match the Python initial HP before the first referee message.
  const int hp = msg ? msg->current_hp : 400;
  const bool low = hp <= threshold.value();
  if (config().output_ports.count("hp_low")) {
    setOutput("hp_low", low);
  }
  return low ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}

TrackHeat::TrackHeat(const std::string & name, const BT::NodeConfig & config)
: BT::StatefulActionNode(name, config)
{
}

BT::PortsList TrackHeat::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::RobotHeat>(
      "heat_port", "{@referee_robotHeat}", "17mm shooter heat message"),
    BT::InputPort<int>("stop_heat", 240, "Disable aim at or above this heat"),
    BT::InputPort<int>("resume_heat", 50, "Resume aim at or below this heat"),
    BT::OutputPort<int>("aim_allowed", "0 or 1 for PublishAutoAim.value"),
  };
}

BT::NodeStatus TrackHeat::onStart()
{
  overheated_ = false;
  const auto status = onRunning();
  RCLCPP_INFO(
    rclcpp::get_logger("TrackHeat"), "%s started: aim_allowed=%d", name().c_str(),
    overheated_ ? 0 : 1);
  return status;
}

BT::NodeStatus TrackHeat::onRunning()
{
  const auto stop = getInput<int>("stop_heat");
  const auto resume = getInput<int>("resume_heat");
  if (!stop || !resume || resume.value() < 0 || stop.value() > 65535 ||
    resume.value() >= stop.value())
  {
    throw BT::RuntimeError("TrackHeat: require 0 <= resume_heat < stop_heat <= 65535");
  }
  const auto msg = getInput<dji_referee_protocol::msg::RobotHeat>("heat_port");
  // Match the Python initial heat before the first referee message.
  const int heat = msg ? msg->shooter_17mm_barrel_heat : 0;
  const bool previously_overheated = overheated_;
  if (!overheated_ && heat >= stop.value()) {
    overheated_ = true;
  } else if (overheated_ && heat <= resume.value()) {
    overheated_ = false;
  }
  setOutput("aim_allowed", overheated_ ? 0 : 1);
  if (previously_overheated != overheated_) {
    RCLCPP_INFO(
      rclcpp::get_logger("TrackHeat"), "%s heat=%d aim_allowed=%d", name().c_str(), heat,
      overheated_ ? 0 : 1);
  }
  return BT::NodeStatus::RUNNING;
}

void TrackHeat::onHalted()
{
  overheated_ = false;
  setOutput("aim_allowed", 0);
}

}  // namespace pb2025_sentry_behavior

BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<pb2025_sentry_behavior::IsHpLow>("IsHpLow");
  factory.registerNodeType<pb2025_sentry_behavior::TrackHeat>("TrackHeat");
}
