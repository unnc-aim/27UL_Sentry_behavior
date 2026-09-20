#ifndef PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_MAP_READY_HPP_
#define PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_MAP_READY_HPP_

#include <memory>
#include <string>

#include "behaviortree_cpp/condition_node.h"
#include "behaviortree_ros2/ros_node_params.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "rclcpp/rclcpp.hpp"

namespace pb2025_sentry_behavior
{
/// INIT gate of the competition state machine: wait for the first /map message,
/// then wait `settle_time` seconds so downstream costmaps are populated.
///
/// Returns FAILURE while the map is missing or still settling, SUCCESS once ready.
/// The Python controller aborted INIT after a timeout; here the timeout is reported
/// as an error log only, so the tree keeps polling until the action is cancelled.
class IsMapReadyCondition : public BT::ConditionNode
{
public:
  IsMapReadyCondition(
    const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params);

  static BT::PortsList providedPorts();

  BT::NodeStatus tick() override;

private:
  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_sub_;
  bool map_received_ = false;
  rclcpp::Time map_received_at_;
  rclcpp::Time start_time_;
};
}  // namespace pb2025_sentry_behavior

#endif  // PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_MAP_READY_HPP_
