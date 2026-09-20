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

#include "pb2025_sentry_behavior/plugins/action/pub_auto_aim.hpp"

namespace pb2025_sentry_behavior
{

PublishAutoAimAction::PublishAutoAimAction(
  const std::string & name, const BT::NodeConfig & config, const BT::RosNodeParams & params)
: RosTopicPubStatefulActionNode(name, config, params)
{
}

BT::PortsList PublishAutoAimAction::providedPorts()
{
  return providedBasicPorts({
    BT::InputPort<int>("value", 0, "Auto-aim switch value (0=off, 1=on)"),
  });
}

bool PublishAutoAimAction::setMessage(std_msgs::msg::Int32 & msg)
{
  int value = 0;
  getInput("value", value);
  msg.data = value;
  return true;
}

bool PublishAutoAimAction::setHaltMessage(std_msgs::msg::Int32 & msg)
{
  msg.data = 0;
  return true;
}

}  // namespace pb2025_sentry_behavior

#include "behaviortree_ros2/plugins.hpp"
CreateRosNodePlugin(pb2025_sentry_behavior::PublishAutoAimAction, "PublishAutoAim");
