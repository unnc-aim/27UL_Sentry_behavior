#include "behavior/plugins/condition/competition_conditions.hpp"

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"
#include "rclcpp/rclcpp.hpp"

namespace behavior
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
  if (hp != previous_hp_ || threshold.value() != previous_threshold_) {
    RCLCPP_INFO(
      rclcpp::get_logger("IsHpLow"), "%s hp=%d threshold=%d hp_low=%d", name().c_str(), hp,
      threshold.value(), low ? 1 : 0);
    previous_hp_ = hp;
    previous_threshold_ = threshold.value();
  }
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

WaitForRecovery::WaitForRecovery(const std::string & name, const BT::NodeConfig & config)
: BT::StatefulActionNode(name, config)
{
}

BT::PortsList WaitForRecovery::providedPorts()
{
  return {
    BT::InputPort<dji_referee_protocol::msg::RobotPerformance>(
      "hp_port", "{@referee_robotPerformance}", "Self robot performance message"),
    BT::InputPort<int>("full_hp", 400, "Leave immediately at or above this HP"),
    BT::InputPort<int>("enough_hp", 350, "Leave after patience at or above this HP"),
    BT::InputPort<int>("patience_ms", 30000, "Recovery wait in wall-clock milliseconds"),
  };
}

BT::NodeStatus WaitForRecovery::onStart()
{
  const auto full = getInput<int>("full_hp");
  const auto enough = getInput<int>("enough_hp");
  const auto patience = getInput<int>("patience_ms");
  if (!full || !enough || !patience || enough.value() < 0 ||
    enough.value() >= full.value() || full.value() > 65535 || patience.value() < 0)
  {
    throw BT::RuntimeError(
      "WaitForRecovery: require 0 <= enough_hp < full_hp <= 65535 and patience_ms >= 0");
  }
  const auto msg = getInput<dji_referee_protocol::msg::RobotPerformance>("hp_port");
  hp_snapshot_ = msg ? msg->current_hp : 400;
  started_at_ = std::chrono::system_clock::now();
  return onRunning();
}

BT::NodeStatus WaitForRecovery::onRunning()
{
  const auto msg = getInput<dji_referee_protocol::msg::RobotPerformance>("hp_port");
  const int hp = msg ? msg->current_hp : 400;
  const int full = getInput<int>("full_hp").value();
  const int enough = getInput<int>("enough_hp").value();
  const int patience = getInput<int>("patience_ms").value();

  if (hp >= full) {
    return BT::NodeStatus::SUCCESS;
  }
  const auto now = std::chrono::system_clock::now();
  if (hp >= enough && now - started_at_ > std::chrono::milliseconds(patience)) {
    return BT::NodeStatus::SUCCESS;
  }
  // Match Python's running-minimum snapshot and its check order.
  if (hp < hp_snapshot_) {
    hp_snapshot_ = hp;
    started_at_ = std::chrono::system_clock::now();
  }
  if (hp <= 0) {
    started_at_ = std::chrono::system_clock::now();
  }
  return BT::NodeStatus::RUNNING;
}

void WaitForRecovery::onHalted()
{
  hp_snapshot_ = 400;
  started_at_ = {};
}

}  // namespace behavior

BT_REGISTER_NODES(factory)
{
  factory.registerNodeType<behavior::IsHpLow>("IsHpLow");
  factory.registerNodeType<behavior::TrackHeat>("TrackHeat");
  factory.registerNodeType<behavior::WaitForRecovery>("WaitForRecovery");
}
