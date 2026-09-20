#include "pb2025_sentry_behavior/plugins/condition/is_tf_ready.hpp"

namespace pb2025_sentry_behavior
{

IsTfReadyCondition::IsTfReadyCondition(
  const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params)
: BT::ConditionNode(name, config), node_(params.nh.lock())
{
  if (!node_) {
    throw BT::RuntimeError("IsTfReady: the ROS node went out of scope");
  }

  tf_buffer_ = std::make_shared<tf2_ros::Buffer>(node_->get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

  start_time_ = node_->now();
}

BT::PortsList IsTfReadyCondition::providedPorts()
{
  return {
    BT::InputPort<std::string>("map_frame", "map", "Parent frame of the localization lookup"),
    BT::InputPort<std::string>("base_frame", "base_footprint", "Robot base frame"),
    BT::InputPort<double>("timeout", 120.0, "Seconds before the missing transform is logged"),
  };
}

BT::NodeStatus IsTfReadyCondition::tick()
{
  std::string map_frame = "map";
  std::string base_frame = "base_footprint";
  double timeout = 120.0;
  getInput("map_frame", map_frame);
  getInput("base_frame", base_frame);
  getInput("timeout", timeout);

  try {
    tf_buffer_->lookupTransform(map_frame, base_frame, tf2::TimePointZero);
  } catch (const tf2::TransformException & ex) {
    if (timeout > 0.0 && (node_->now() - start_time_).seconds() > timeout) {
      RCLCPP_ERROR_THROTTLE(
        node_->get_logger(), *node_->get_clock(), 5000,
        "IsTfReady: transform %s -> %s still unavailable after %.1fs: %s", map_frame.c_str(),
        base_frame.c_str(), timeout, ex.what());
    } else {
      RCLCPP_INFO_THROTTLE(
        node_->get_logger(), *node_->get_clock(), 2000,
        "IsTfReady: waiting for transform %s -> %s", map_frame.c_str(), base_frame.c_str());
    }
    return BT::NodeStatus::FAILURE;
  }

  RCLCPP_INFO_ONCE(
    node_->get_logger(), "IsTfReady: transform %s -> %s ready", map_frame.c_str(),
    base_frame.c_str());
  return BT::NodeStatus::SUCCESS;
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(pb2025_sentry_behavior::IsTfReadyCondition, "IsTfReady");
