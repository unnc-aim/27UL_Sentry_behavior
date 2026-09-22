#include <iostream>
#include <stdexcept>
#include <string>

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"

namespace
{
void require(bool condition, const std::string & message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace

int main(int argc, char ** argv)
{
  try {
    require(argc == 2, "Pass the competition_conditions plugin library path");
    BT::BehaviorTreeFactory factory;
    factory.registerFromPlugin(argv[1]);
    auto global = BT::Blackboard::create();
    auto local = BT::Blackboard::create(global);
    auto hp_tree = factory.createTreeFromText(R"(
      <root BTCPP_format="4"><BehaviorTree ID="hp_test">
        <IsHpLow threshold="150" hp_low="{hp_low}"/>
      </BehaviorTree></root>)", local);
    require(hp_tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Initial HP must be 400");
    dji_referee_protocol::msg::RobotPerformance hp;
    for (const int value : {151, 150, 0, 400}) {
      hp.current_hp = value;
      global->set("referee_robotPerformance", hp);
      const auto status = hp_tree.tickExactlyOnce();
      require(
        status == (value <= 150 ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE),
        "HP threshold/global blackboard mapping");
      require(local->get<bool>("hp_low") == (value <= 150), "HP diagnostic output");
    }

    bool enabled = true;
    int observed_aim = -1;
    factory.registerSimpleCondition("Enabled", [&enabled](BT::TreeNode &) {
      return enabled ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
    });
    factory.registerSimpleAction("ObserveAim", [&observed_aim, local](BT::TreeNode &) {
      observed_aim = local->get<int>("aim_allowed");
      return BT::NodeStatus::SUCCESS;
    });
    auto heat_tree = factory.createTreeFromText(R"(
      <root BTCPP_format="4"><BehaviorTree ID="heat_test">
        <ReactiveSequence>
          <Enabled/>
          <Parallel success_count="-1" failure_count="1">
            <TrackHeat aim_allowed="{aim_allowed}"/>
            <KeepRunningUntilFailure><ObserveAim/></KeepRunningUntilFailure>
          </Parallel>
        </ReactiveSequence>
      </BehaviorTree></root>)", local);
    require(heat_tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Initial heat is zero");
    require(observed_aim == 1, "Initial aim output");
    dji_referee_protocol::msg::RobotHeat heat;
    const int heats[] = {239, 240, 100, 51, 50, 51, 240};
    const int aims[] = {1, 0, 0, 0, 1, 1, 0};
    for (int i = 0; i < 7; ++i) {
      heat.shooter_17mm_barrel_heat = heats[i];
      global->set("referee_robotHeat", heat);
      require(heat_tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Heat must stay RUNNING");
      require(observed_aim == aims[i], "Hysteresis and same-tick consumer ordering");
    }
    enabled = false;
    require(heat_tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Gate must halt heat");
    require(local->get<int>("aim_allowed") == 0, "Halt disables output");
    enabled = true;
    heat.shooter_17mm_barrel_heat = 100;
    global->set("referee_robotHeat", heat);
    require(heat_tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Re-entry");
    require(observed_aim == 1, "Re-entry must clear previous overheated latch");
    heat_tree.haltTree();

    auto custom_hp = factory.createTreeFromText(R"(
      <root BTCPP_format="4"><BehaviorTree ID="custom_hp">
        <IsHpLow threshold="400"/>
      </BehaviorTree></root>)", local);
    require(custom_hp.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Custom HP threshold");
    auto custom_heat = factory.createTreeFromText(R"(
      <root BTCPP_format="4"><BehaviorTree ID="custom_heat">
        <TrackHeat stop_heat="100" resume_heat="20" aim_allowed="{aim_allowed}"/>
      </BehaviorTree></root>)", local);
    require(custom_heat.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Custom heat threshold");
    require(local->get<int>("aim_allowed") == 0, "Custom heat threshold applied");
    custom_heat.haltTree();

    for (const auto & xml : {
        R"(<root BTCPP_format="4"><BehaviorTree ID="bad_hp"><IsHpLow threshold="-1"/></BehaviorTree></root>)",
        R"(<root BTCPP_format="4"><BehaviorTree ID="bad_heat"><TrackHeat stop_heat="50" resume_heat="50" aim_allowed="{aim_allowed}"/></BehaviorTree></root>)"})
    {
      bool rejected = false;
      try {
        auto invalid = factory.createTreeFromText(xml, local);
        invalid.tickExactlyOnce();
      } catch (const std::exception &) {
        rejected = true;
      }
      require(rejected, "Invalid thresholds must be rejected");
    }
    std::cout << "Competition conditions: all checks passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
