#include "behavior/plugins/condition/is_nav2_ready.hpp"

namespace pb2025_sentry_behavior
{

IsNav2ReadyCondition::IsNav2ReadyCondition(
  const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params)
: BT::ConditionNode(name, config), node_(params.nh.lock())
{
  if (!node_) {
    throw BT::RuntimeError("IsNav2Ready: the ROS node went out of scope");
  }

  getInput("action_name", action_name_);
  callback_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  client_ = rclcpp_action::create_client<NavigateToPose>(node_, action_name_, callback_group_);

  start_time_ = node_->now();
}

BT::PortsList IsNav2ReadyCondition::providedPorts()
{
  return {
    BT::InputPort<std::string>("action_name", "navigate_to_pose", "Action server to wait for"),
    BT::InputPort<double>("timeout", 30.0, "Seconds before the missing server is logged"),
  };
}

BT::NodeStatus IsNav2ReadyCondition::tick()
{
  if (client_ && client_->action_server_is_ready()) {
    RCLCPP_INFO_ONCE(
      node_->get_logger(), "IsNav2Ready: action server '%s' ready", action_name_.c_str());
    return BT::NodeStatus::SUCCESS;
  }

  double timeout = 30.0;
  getInput("timeout", timeout);

  if (timeout > 0.0 && (node_->now() - start_time_).seconds() > timeout) {
    RCLCPP_ERROR_THROTTLE(
      node_->get_logger(), *node_->get_clock(), 5000,
      "IsNav2Ready: action server '%s' still unavailable after %.1fs", action_name_.c_str(),
      timeout);
  } else {
    RCLCPP_INFO_THROTTLE(
      node_->get_logger(), *node_->get_clock(), 2000,
      "IsNav2Ready: waiting for action server '%s' ...", action_name_.c_str());
  }
  return BT::NodeStatus::FAILURE;
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(pb2025_sentry_behavior::IsNav2ReadyCondition, "IsNav2Ready");
