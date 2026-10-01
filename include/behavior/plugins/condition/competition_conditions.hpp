#ifndef BEHAVIOR__PLUGINS__CONDITION__COMPETITION_CONDITIONS_HPP_
#define BEHAVIOR__PLUGINS__CONDITION__COMPETITION_CONDITIONS_HPP_

#include <chrono>
#include <string>

#include "behaviortree_cpp/action_node.h"
#include "behaviortree_cpp/condition_node.h"

namespace pb2025_sentry_behavior
{

class IsHpLow : public BT::ConditionNode
{
public:
  IsHpLow(const std::string & name, const BT::NodeConfig & config);
  static BT::PortsList providedPorts();
  BT::NodeStatus tick() override;
};

class TrackHeat : public BT::StatefulActionNode
{
public:
  TrackHeat(const std::string & name, const BT::NodeConfig & config);
  static BT::PortsList providedPorts();
  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  bool overheated_ = false;
};

class WaitForRecovery : public BT::StatefulActionNode
{
public:
  WaitForRecovery(const std::string & name, const BT::NodeConfig & config);
  static BT::PortsList providedPorts();
  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  int hp_snapshot_ = 400;
  std::chrono::system_clock::time_point started_at_;
};

}  // namespace pb2025_sentry_behavior

#endif  // BEHAVIOR__PLUGINS__CONDITION__COMPETITION_CONDITIONS_HPP_
