#ifndef BEHAVIOR__PLUGINS__CONDITION__IS_NAV2_READY_HPP_
#define BEHAVIOR__PLUGINS__CONDITION__IS_NAV2_READY_HPP_

#include <memory>
#include <string>

#include "behaviortree_cpp/condition_node.h"
#include "behaviortree_ros2/ros_node_params.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

namespace pb2025_sentry_behavior
{
/// INIT gate of the competition state machine: wait until the Nav2 action server
/// (`navigate_to_pose` by default) is discoverable.
///
/// Returns FAILURE while the server is missing, SUCCESS once it is ready.
/// The Python controller aborted INIT after a timeout; here the timeout is reported
/// as an error log only, so the tree keeps polling until the action is cancelled.
class IsNav2ReadyCondition : public BT::ConditionNode
{
public:
  using NavigateToPose = nav2_msgs::action::NavigateToPose;

  IsNav2ReadyCondition(
    const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params);

  static BT::PortsList providedPorts();

  BT::NodeStatus tick() override;

private:
  rclcpp::Node::SharedPtr node_;
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr client_;
  std::string action_name_;
  rclcpp::Time start_time_;
};
}  // namespace pb2025_sentry_behavior

#endif  // BEHAVIOR__PLUGINS__CONDITION__IS_NAV2_READY_HPP_
