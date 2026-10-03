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

#include "behavior/plugins/action/send_nav2_goal.hpp"

#include "behavior/custom_types.hpp"

namespace behavior
{

SendNav2GoalAction::SendNav2GoalAction(
  const std::string & name, const BT::NodeConfig & conf, const BT::RosNodeParams & params)
: RosActionNode<nav2_msgs::action::NavigateToPose>(name, conf, params)
{
}

// bool: 返回的值类型为布尔值
// SendNav2GoalAction:: 这里::是作用域运算符，表示这个函数属于SendNav2GoalAction::类
// 输入：nav2_msgs::action::NavigateToPose::Goal，nav2 导航到指定位置动作的目标消息类型
// & goal: 通过引用接受消息
bool SendNav2GoalAction::setGoal(nav2_msgs::action::NavigateToPose::Goal & goal)
{
  // 从名为"goal"的行为树输入端口，取出端口当前提供的一条PoseStamped消息
  // getInput<类型>(端口名)：模板函数，获取输入，尖括号内指定期望读取的数据类型。
  // geometry_msgs::msg::PoseStamped：一种带时间和坐标系信息的位姿消息类型
  // "goal": 指定端口名称的字符串
  auto receive_goal = getInput<geometry_msgs::msg::PoseStamped>("goal");

  // 如果读取失败，则提示缺少必须的输入目标，然后结束函数并报告失败
  if (!receive_goal) {
    RCLCPP_ERROR(logger(), "Missing required input [goal]");
    return false;
  }

  // 修改引用的输入变量 goal
  // 填写目标位姿的坐标系名称，注意这里默认输入的位置和朝向已用map坐标系表达
  // !!!注意!!! 在复用预建图模式下的坐标点，其原点是预建立的图内已经定死的，并非车启动时amcl重定位的车当前所在位置
  goal.pose.header.frame_id = "map";
  // 调用now获取当前时间，写入目标消息的时间戳
  goal.pose.header.stamp = now();
  // 范文读取成功后保存的posestamped消息，再取出其中的pose成员
  // -> 是运算符，左侧可以是指针，也可以是一个类对象
  goal.pose.pose = receive_goal->pose;

  return true;
}

BT::NodeStatus SendNav2GoalAction::onResultReceived(const WrappedResult & wr)
{
  switch (wr.code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      RCLCPP_INFO(logger(), "Navigation succeeded!");
      return BT::NodeStatus::SUCCESS;

    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_ERROR(logger(), "Navigation aborted by server");
      return BT::NodeStatus::FAILURE;

    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_WARN(logger(), "Navigation canceled");
      return BT::NodeStatus::FAILURE;

    default:
      RCLCPP_ERROR(logger(), "Unknown navigation result code: %d", static_cast<int>(wr.code));
      return BT::NodeStatus::FAILURE;
  }
}

BT::NodeStatus SendNav2GoalAction::onFeedback(
  const std::shared_ptr<const nav2_msgs::action::NavigateToPose::Feedback> feedback)
{
  RCLCPP_DEBUG(logger(), "Distance remaining: %f", feedback->distance_remaining);
  return BT::NodeStatus::RUNNING;
}

void SendNav2GoalAction::onHalt() { RCLCPP_INFO(logger(), "SendNav2GoalAction has been halted."); }

BT::NodeStatus SendNav2GoalAction::onFailure(BT::ActionNodeErrorCode error)
{
  RCLCPP_ERROR(
    logger(), "SendNav2GoalAction failed: %s (%d)", BT::toStr(error),
    static_cast<int>(error));
  return BT::NodeStatus::FAILURE;
}

BT::PortsList SendNav2GoalAction::providedPorts()
{
  BT::PortsList additional_ports = {
    BT::InputPort<geometry_msgs::msg::PoseStamped>(
      "goal", "0;0;0", "Expected goal pose that send to nav2. Fill with format `x;y;yaw`"),
  };
  return providedBasicPorts(additional_ports);
}

}  // namespace behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(behavior::SendNav2GoalAction, "SendNav2Goal");

/*
呐，你知道吗？听说樱花飘落的速度是秒速五厘米哦。
*/