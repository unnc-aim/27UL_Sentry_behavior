#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "behavior/plugins/condition/is_nav2_ready.hpp"
#include "behaviortree_cpp/bt_factory.h"

using namespace std::chrono_literals;

namespace
{
void require(bool condition, const std::string & message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template<typename Action>
auto makeServer(const rclcpp::Node::SharedPtr & node, const std::string & name)
{
  // 检查只查询服务是否存在；模拟服务拒绝目标请求。
  return rclcpp_action::create_server<Action>(node, name,
    [](const auto &, const auto &) {return rclcpp_action::GoalResponse::REJECT;},
    [](const auto &) {return rclcpp_action::CancelResponse::REJECT;},
    [](const auto &) {});
}
}  // namespace

int main(int argc, char ** argv)
{
  const char * domain = std::getenv("ROS_DOMAIN_ID");
  const char * localhost = std::getenv("ROS_LOCALHOST_ONLY");
  if (!domain || std::string(domain) == "0" || !localhost || std::string(localhost) != "1") {
    std::cerr << "Set ROS_DOMAIN_ID=189 ROS_LOCALHOST_ONLY=1\n";
    return 2;
  }
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<rclcpp::Node>("nav2_readiness_check");
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    BT::BehaviorTreeFactory factory;
    factory.registerNodeType<behavior::IsNav2ReadyCondition>(
      "IsNav2Ready", BT::RosNodeParams(node));
    auto tree = [&](const std::string & attributes) {
        return factory.createTreeFromText(
          "<root BTCPP_format=\"4\"><BehaviorTree ID=\"check\"><IsNav2Ready " +
          attributes + " timeout=\"0\"/></BehaviorTree></root>");
      };
    auto single = tree("action_name=\"/readiness_test_single\"");  // 省略新端口，检查单点默认值。
    auto multiple = tree(
      "action_name=\"/readiness_test_multiple\" through_poses=\"true\"");
    require(single.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Absent single server");
    require(multiple.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Absent multiple server");

    auto single_server = makeServer<nav2_msgs::action::NavigateToPose>(
      node, "/readiness_test_single");
    auto multiple_server = makeServer<nav2_msgs::action::NavigateThroughPoses>(
      node, "/readiness_test_multiple");
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    bool ready = false;
    while (std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      const bool single_ready = single.tickExactlyOnce() == BT::NodeStatus::SUCCESS;
      const bool multiple_ready = multiple.tickExactlyOnce() == BT::NodeStatus::SUCCESS;
      if (single_ready && multiple_ready) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    require(ready, "Matching Action servers become ready");
    std::cout << "Nav2 readiness: all checks passed\n";
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
