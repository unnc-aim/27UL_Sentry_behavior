#include "pb2025_sentry_behavior/plugins/condition/is_map_ready.hpp"

namespace pb2025_sentry_behavior
{

IsMapReadyCondition::IsMapReadyCondition(
  const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params)
: BT::ConditionNode(name, config), node_(params.nh.lock())
{
  if (!node_) {
    throw BT::RuntimeError("IsMapReady: the ROS node went out of scope");
  }

  std::string topic_name = "/map";
  getInput("topic_name", topic_name);

  // TRANSIENT_LOCAL + RELIABLE lets a late subscriber receive the retained map.
  auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable();
  map_sub_ = node_->create_subscription<nav_msgs::msg::OccupancyGrid>(
    topic_name, qos, [this](const nav_msgs::msg::OccupancyGrid::SharedPtr msg) {
      if (map_received_) {
        return;
      }
      map_received_ = true;
      map_received_at_ = node_->now();
      RCLCPP_INFO(
        node_->get_logger(), "IsMapReady: map %ux%u received on latched topic",
        msg->info.width, msg->info.height);
    });

  start_time_ = node_->now();
}

BT::PortsList IsMapReadyCondition::providedPorts()
{
  return {
    BT::InputPort<std::string>("topic_name", "/map", "Latched OccupancyGrid topic to wait for"),
    BT::InputPort<double>("settle_time", 5.0, "Seconds to keep waiting after the first map"),
    BT::InputPort<double>("timeout", 120.0, "Seconds before the missing map is logged as an error"),
  };
}

BT::NodeStatus IsMapReadyCondition::tick()
{
  double settle_time = 5.0;
  double timeout = 120.0;
  getInput("settle_time", settle_time);
  getInput("timeout", timeout);

  if (!map_received_) {
    if (timeout > 0.0 && (node_->now() - start_time_).seconds() > timeout) {
      RCLCPP_ERROR_THROTTLE(
        node_->get_logger(), *node_->get_clock(), 5000,
        "IsMapReady: no /map message after %.1fs, still waiting (INIT not ready)", timeout);
    } else {
      RCLCPP_INFO_THROTTLE(
        node_->get_logger(), *node_->get_clock(), 2000, "IsMapReady: waiting for /map ...");
    }
    return BT::NodeStatus::FAILURE;
  }

  const double settled_for = (node_->now() - map_received_at_).seconds();
  if (settled_for < settle_time) {
    RCLCPP_INFO_THROTTLE(
      node_->get_logger(), *node_->get_clock(), 1000,
      "IsMapReady: map received, settling %.1f/%.1fs ...", settled_for, settle_time);
    return BT::NodeStatus::FAILURE;
  }

  RCLCPP_INFO_ONCE(node_->get_logger(), "IsMapReady: map ready");
  return BT::NodeStatus::SUCCESS;
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(pb2025_sentry_behavior::IsMapReadyCondition, "IsMapReady");
