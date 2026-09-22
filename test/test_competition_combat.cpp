#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/game_status.hpp"
#include "dji_referee_protocol/msg/robot_heat.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"
#include "std_msgs/msg/int32.hpp"

namespace
{
void require(bool condition, const std::string & message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Output
{
  std::string topic;
  double value;
};
}  // namespace

int main(int argc, char ** argv)
{
  try {
    require(argc == 6, "Pass conditions/game/manual plugins and combat/debug XML paths");
    BT::BehaviorTreeFactory factory;
    for (int i = 1; i <= 3; ++i) {
      factory.registerFromPlugin(argv[i]);
    }
    std::vector<Output> outputs;
    bool fail_next_aim = false;
    auto publisher = [&](const std::string & id, const std::string & topic,
        const std::string & value_port, BT::PortsList ports) {
        ports.insert(BT::InputPort<std::string>("topic_name"));
        ports.insert(BT::InputPort<int>("duration", 0));
        factory.registerSimpleAction(id, [&, topic, value_port](BT::TreeNode & node) {
          require(node.getInput<std::string>("topic_name").value() == topic, "Output topic");
          require(node.getInput<int>("duration").value() == 0, "One publish per tick");
          if (topic == "auto_aim_switch" && fail_next_aim) {
            fail_next_aim = false;
            return BT::NodeStatus::FAILURE;
          }
          if (topic == "cmd_vel") {
            require(node.getInput<double>("v_y").value() == 0, "Stop lateral velocity");
            require(node.getInput<double>("v_yaw").value() == 0, "Stop angular velocity");
          }
          if (topic == "gimbal_scan_cmd") {
            require(node.getInput<double>("gimbal_vel_pitch").value() == 0, "Pitch speed");
            require(node.getInput<double>("yaw_min").value() == -3.14159, "Yaw min");
            require(node.getInput<double>("yaw_max").value() == 3.14159, "Yaw max");
            require(node.getInput<double>("pitch_min").value() == -0.3, "Pitch min");
            require(node.getInput<double>("pitch_max").value() == 0.3, "Pitch max");
          }
          const double value = topic == "auto_aim_switch" ?
            node.getInput<int>(value_port).value() : node.getInput<double>(value_port).value();
          outputs.push_back({topic, value});
          return BT::NodeStatus::SUCCESS;
        }, ports);
      };
    publisher("PublishTwist", "cmd_vel", "v_x", {
      BT::InputPort<double>("v_x"), BT::InputPort<double>("v_y"),
      BT::InputPort<double>("v_yaw")});
    publisher("PublishSpinSpeed", "cmd_spin", "spin_speed", {
      BT::InputPort<double>("spin_speed")});
    publisher("PublishAutoAim", "auto_aim_switch", "value", {BT::InputPort<int>("value")});
    publisher("PublishGimbalVelocity", "gimbal_scan_cmd", "gimbal_vel_yaw", {
      BT::InputPort<double>("gimbal_vel_yaw"), BT::InputPort<double>("gimbal_vel_pitch"),
      BT::InputPort<double>("yaw_min"), BT::InputPort<double>("yaw_max"),
      BT::InputPort<double>("pitch_min"), BT::InputPort<double>("pitch_max")});
    factory.registerSimpleAction("PrintRefereeStatus", [](BT::TreeNode &) {
      return BT::NodeStatus::SUCCESS;
    });
    factory.registerBehaviorTreeFromFile(argv[4]);
    factory.registerBehaviorTreeFromFile(argv[5]);
    auto global = BT::Blackboard::create();
    auto local = BT::Blackboard::create(global);
    dji_referee_protocol::msg::GameStatus game;
    dji_referee_protocol::msg::RobotPerformance hp;
    dji_referee_protocol::msg::RobotHeat heat;
    auto count = [&](const std::string & topic) {
        size_t result = 0;
        for (const auto & output : outputs) {
          result += output.topic == topic;
        }
        return result;
      };
    auto last = [&](const std::string & topic) {
        for (auto it = outputs.rbegin(); it != outputs.rend(); ++it) {
          if (it->topic == topic) {
            return it->value;
          }
        }
        throw std::runtime_error("No output on " + topic);
      };
    auto tree = factory.createTree("competition_combat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Missing game must fail");
    require(count("cmd_vel") == 0 && last("auto_aim_switch") == 0, "No entry before game");
    game.game_progress = 4;
    game.stage_remain_time = 65535;
    global->set("referee_gameStatus", game);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Default HP/heat, progress only");
    require(count("cmd_vel") == 1 && last("cmd_vel") == 0, "Entry stop exactly once");
    require(last("cmd_spin") == 7 && last("gimbal_scan_cmd") == 1, "Combat constants");
    require(last("auto_aim_switch") == 1, "Initial aim enabled");
    hp.current_hp = 151;
    global->set("referee_robotPerformance", hp);
    const int heats[] = {239, 240, 100, 51, 50, 51};
    const int aims[] = {1, 0, 0, 0, 1, 1};
    for (int i = 0; i < 6; ++i) {
      heat.shooter_17mm_barrel_heat = heats[i];
      global->set("referee_robotHeat", heat);
      outputs.clear();
      require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Heat does not end combat");
      require(outputs.size() == 3 && count("cmd_vel") == 0, "Three heartbeats, no Twist");
      require(count("cmd_spin") == 1 && count("gimbal_scan_cmd") == 1 &&
        count("auto_aim_switch") == 1, "Each output repeats every tick");
      require(last("auto_aim_switch") == aims[i], "Same-tick heat output");
    }
    hp.current_hp = 150;
    global->set("referee_robotPerformance", hp);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Low HP advances to retreat");
    require(outputs.size() == 1 && last("auto_aim_switch") == 0, "Explicit low-HP aim off");
    hp.current_hp = 400;
    global->set("referee_robotPerformance", hp);
    heat.shooter_17mm_barrel_heat = 240;
    global->set("referee_robotHeat", heat);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Re-enter hot combat");
    require(last("auto_aim_switch") == 0 && count("cmd_vel") == 1, "Hot entry and stop");
    game.game_progress = 5;
    hp.current_hp = 0;
    global->set("referee_gameStatus", game);
    global->set("referee_robotPerformance", hp);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Game end wins over low HP");
    require(outputs.size() == 1 && last("auto_aim_switch") == 0, "Explicit game-end aim off");
    game.game_progress = 4;
    hp.current_hp = 400;
    heat.shooter_17mm_barrel_heat = 100;
    global->set("referee_gameStatus", game);
    global->set("referee_robotPerformance", hp);
    global->set("referee_robotHeat", heat);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Re-entry after gate halt");
    require(last("auto_aim_switch") == 1, "Halt clears heat latch");
    hp.current_hp = 150;
    global->set("referee_robotPerformance", hp);
    fail_next_aim = true;
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Publish failure cannot advance");
    require(outputs.size() == 1 && last("auto_aim_switch") == 0, "Failure cleanup closes aim");
    tree.haltTree();

    // The debug wrapper uses manual start instead of the game gate and exits once.
    game.game_progress = 0;
    global->set("referee_gameStatus", game);
    for (const bool low_hp_exit : {false, true}) {
      std_msgs::msg::Int32 manual;
      manual.data = 0;
      hp.current_hp = 400;
      global->set("manual_start", manual);
      global->set("referee_robotPerformance", hp);
      auto debug = factory.createTree("competition_test_combat", local);
      outputs.clear();
      require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Await manual start");
      require(outputs.empty(), "No control output before start");
      manual.data = 1;
      global->set("manual_start", manual);
      std::this_thread::sleep_for(std::chrono::milliseconds(220));
      require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Manual starts without game");
      require(last("cmd_spin") == 7, "Debug executes real combat XML");
      manual.data = low_hp_exit ? 1 : 0;
      hp.current_hp = low_hp_exit ? 150 : 400;
      global->set("manual_start", manual);
      global->set("referee_robotPerformance", hp);
      outputs.clear();
      require(debug.tickExactlyOnce() == (low_hp_exit ? BT::NodeStatus::SUCCESS :
        BT::NodeStatus::FAILURE), "Debug preserves exit status");
      require(last("auto_aim_switch") == 0 && last("cmd_spin") == 0 &&
        last("gimbal_scan_cmd") == 0 && count("cmd_vel") == 1, "Debug exit stops outputs");
    }
    std::cout << "Competition combat: all checks passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
