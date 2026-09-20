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

#include "pb2025_sentry_behavior/plugins/action/send_nav2_through_poses.hpp"

#include "pb2025_sentry_behavior/custom_types.hpp"

namespace pb2025_sentry_behavior
{

SendNav2ThroughPosesAction::SendNav2ThroughPosesAction(
  const std::string & name, const BT::NodeConfig & conf, const BT::RosNodeParams & params)
: RosActionNode<nav2_msgs::action::NavigateThroughPoses>(name, conf, params)
{
}

bool SendNav2ThroughPosesAction::setGoal(
  nav2_msgs::action::NavigateThroughPoses::Goal & goal)
{
  auto goals_str = getInput<std::string>("goals");
  if (!goals_str) {
    RCLCPP_ERROR(logger(), "Missing required input [goals]");
    return false;
  }

  // Parse "x1;y1;yaw1|x2;y2;yaw2|..." format
  goal.poses.clear();
  auto pose_parts = BT::splitString(*goals_str, '|');
  for (const auto & part : pose_parts) {
    auto pose = BT::convertFromString<geometry_msgs::msg::PoseStamped>(part);
    pose.header.frame_id = "map";
    pose.header.stamp = now();
    goal.poses.push_back(pose);
  }

  if (goal.poses.empty()) {
    RCLCPP_ERROR(logger(), "No valid poses parsed from goals input");
    return false;
  }

  RCLCPP_INFO(logger(), "Sending %zu poses to NavigateThroughPoses", goal.poses.size());
  return true;
}

BT::NodeStatus SendNav2ThroughPosesAction::onResultReceived(const WrappedResult & wr)
{
  switch (wr.code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      RCLCPP_INFO(logger(), "NavigateThroughPoses succeeded!");
      return BT::NodeStatus::SUCCESS;

    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_ERROR(logger(), "NavigateThroughPoses aborted by server");
      return BT::NodeStatus::FAILURE;

    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_WARN(logger(), "NavigateThroughPoses canceled");
      return BT::NodeStatus::FAILURE;

    default:
      RCLCPP_ERROR(
        logger(), "Unknown NavigateThroughPoses result code: %d", static_cast<int>(wr.code));
      return BT::NodeStatus::FAILURE;
  }
}

BT::NodeStatus SendNav2ThroughPosesAction::onFeedback(
  const std::shared_ptr<const nav2_msgs::action::NavigateThroughPoses::Feedback> feedback)
{
  RCLCPP_DEBUG(
    logger(), "Distance remaining: %.2f, poses remaining: %d", feedback->distance_remaining,
    feedback->number_of_poses_remaining);
  return BT::NodeStatus::RUNNING;
}

void SendNav2ThroughPosesAction::onHalt()
{
  RCLCPP_INFO(logger(), "SendNav2ThroughPosesAction has been halted.");
}

BT::NodeStatus SendNav2ThroughPosesAction::onFailure(BT::ActionNodeErrorCode error)
{
  RCLCPP_ERROR(logger(), "SendNav2ThroughPosesAction failed with error code: %d", error);
  return BT::NodeStatus::FAILURE;
}

BT::PortsList SendNav2ThroughPosesAction::providedPorts()
{
  BT::PortsList additional_ports = {
    BT::InputPort<std::string>(
      "goals", "",
      "Waypoints in format 'x1;y1;yaw1|x2;y2;yaw2|...'. "
      "Each pose uses ';' separator, poses separated by '|'."),
  };
  return providedBasicPorts(additional_ports);
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(
  pb2025_sentry_behavior::SendNav2ThroughPosesAction, "SendNav2ThroughPoses");
