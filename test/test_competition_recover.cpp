#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/game_status.hpp"
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
    require(argc == 6, "Pass conditions/game/manual plugins and recover/debug XML paths");
    BT::BehaviorTreeFactory factory;
    for (int i = 1; i <= 3; ++i) {
      factory.registerFromPlugin(argv[i]);
    }
    std::vector<Output> outputs;
    auto publisher = [&](const std::string & id, const std::string & topic,
        const std::string & value_port, BT::PortsList ports) {
        ports.insert(BT::InputPort<std::string>("topic_name"));
        ports.insert(BT::InputPort<int>("duration", 0));
        factory.registerSimpleAction(id, [&, topic, value_port](BT::TreeNode & node) {
          require(node.getInput<std::string>("topic_name").value() == topic, "Output topic");
          require(node.getInput<int>("duration").value() == 0, "One publish per tick");
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
    auto set_game = [&](int progress) {
        game.game_progress = progress;
        game.stage_remain_time = 65535;
        global->set("referee_gameStatus", game);
      };
    auto set_hp = [&](int value) {
        hp.current_hp = value;
        global->set("referee_robotPerformance", hp);
      };

    auto tree = factory.createTree("competition_recover", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Missing game exits recover");
    require(outputs.empty(), "No control output before game gate");

    set_game(4);
    set_hp(399);
    tree = factory.createTree("competition_recover", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "HP 399 waits");
    require(count("cmd_vel") == 1 && last("cmd_vel") == 0, "Entry stop exactly once");
    require(last("auto_aim_switch") == 0 && last("cmd_spin") == 7 &&
      last("gimbal_scan_cmd") == 0.5, "Recover output values");
    for (int i = 0; i < 3; ++i) {
      outputs.clear();
      require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Waiting remains RUNNING");
      require(outputs.size() == 3 && count("cmd_vel") == 0, "Three heartbeats, no Twist");
      require(last("auto_aim_switch") == 0 && last("cmd_spin") == 7 &&
        last("gimbal_scan_cmd") == 0.5, "Waiting output values");
    }
    set_hp(400);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "HP 400 advances");
    require(count("cmd_vel") == 0, "No repeated entry stop on exit");

    set_hp(0);
    outputs.clear();
    tree = factory.createTree("competition_recover", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "HP zero waits for revival");
    set_game(5);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Game end exits recover");
    require(outputs.empty(), "Game end never restarts recover outputs");

    set_game(4);
    set_hp(400);
    outputs.clear();
    tree = factory.createTree("competition_recover", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Full HP on entry advances");
    require(count("cmd_vel") == 1 && last("cmd_spin") == 7, "Entry outputs still publish");

    set_game(0);
    set_hp(399);
    std_msgs::msg::Int32 manual;
    manual.data = 0;
    global->set("manual_start", manual);
    auto debug = factory.createTree("competition_test_recover", local);
    outputs.clear();
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait for manual start");
    require(outputs.empty(), "No output before manual start");
    manual.data = 1;
    global->set("manual_start", manual);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Manual recover starts");
    require(last("cmd_spin") == 7, "Debug uses real recover subtree");
    set_hp(400);
    outputs.clear();
    require(debug.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Manual full-HP exit");
    require(last("cmd_spin") == 0 && last("gimbal_scan_cmd") == 0 &&
      last("auto_aim_switch") == 0 && count("cmd_vel") == 1, "Debug success stops outputs");

    set_hp(399);
    manual.data = 0;
    global->set("manual_start", manual);
    debug = factory.createTree("competition_test_recover", local);
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait for manual restart");
    manual.data = 1;
    global->set("manual_start", manual);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Manual waiting starts");
    manual.data = 0;
    global->set("manual_start", manual);
    outputs.clear();
    require(debug.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Manual stop returns FAILURE");
    require(last("cmd_spin") == 0 && last("gimbal_scan_cmd") == 0 &&
      last("auto_aim_switch") == 0 && count("cmd_vel") == 1, "Debug stop zeros outputs");

    std::cout << "Competition recover: all checks passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
