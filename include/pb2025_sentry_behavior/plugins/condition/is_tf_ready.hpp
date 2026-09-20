#ifndef PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_TF_READY_HPP_
#define PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_TF_READY_HPP_

#include <memory>
#include <string>

#include "behaviortree_cpp/condition_node.h"
#include "behaviortree_ros2/ros_node_params.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

namespace pb2025_sentry_behavior
{
/// INIT gate of the competition state machine: wait until the localization chain
/// can resolve `map -> base_footprint`.
///
/// Returns FAILURE while the transform is unavailable, SUCCESS once it resolves.
/// The Python controller aborted INIT after a timeout; here the timeout is reported
/// as an error log only, so the tree keeps polling until the action is cancelled.
class IsTfReadyCondition : public BT::ConditionNode
{
public:
  IsTfReadyCondition(
    const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params);

  static BT::PortsList providedPorts();

  BT::NodeStatus tick() override;

private:
  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::Time start_time_;
};
}  // namespace pb2025_sentry_behavior

#endif  // PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_TF_READY_HPP_
