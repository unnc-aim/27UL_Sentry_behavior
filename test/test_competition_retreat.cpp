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

class TestNav : public BT::StatefulActionNode
{
public:
  using BT::StatefulActionNode::StatefulActionNode;
  static std::vector<std::string> goals;
  static std::vector<bool> outcomes;
  static bool hold;
  static int halts;

  static BT::PortsList providedPorts()
  {
    return {BT::InputPort<std::string>("goal"), BT::InputPort<std::string>("goals"),
      BT::InputPort<std::string>("action_name")};
  }

  BT::NodeStatus onStart() override
  {
    const auto route = getInput<std::string>("goals");  // 正式多点和手动单点共用记录型节点。
    require(getInput<std::string>("action_name").value() ==
      (route ? "navigate_through_poses" : "navigate_to_pose"), "Nav2 action type");
    goals.push_back(route ? route.value() : getInput<std::string>("goal").value());
    return hold ? BT::NodeStatus::RUNNING : finish();
  }

  BT::NodeStatus onRunning() override {return hold ? BT::NodeStatus::RUNNING : finish();}
  void onHalted() override {++halts;}

private:
  BT::NodeStatus finish()
  {
    const bool success = outcomes.empty() || outcomes.front();
    if (!outcomes.empty()) {
      outcomes.erase(outcomes.begin());
    }
    return success ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
  }
};

std::vector<std::string> TestNav::goals;
std::vector<bool> TestNav::outcomes;
bool TestNav::hold = false;
int TestNav::halts = 0;

struct Output
{
  std::string topic;
  double value;
};
}  // namespace

int main(int argc, char ** argv)
{
  try {
    require(argc == 6, "Pass conditions/game/manual plugins and retreat/debug XML paths");
    BT::BehaviorTreeFactory factory;
    for (int i = 1; i <= 3; ++i) {
      factory.registerFromPlugin(argv[i]);
    }
    factory.registerNodeType<TestNav>("SendNav2Goal");
    factory.registerNodeType<TestNav>("SendNav2ThroughPoses");
    std::vector<Output> outputs;
    auto publisher = [&](const std::string & id, const std::string & topic,
        const std::string & value_port, BT::PortsList ports) {
        ports.insert(BT::InputPort<std::string>("topic_name"));
        ports.insert(BT::InputPort<int>("duration", 0, "Publish duration in milliseconds"));
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
    auto clear = [&] {
        outputs.clear();
        TestNav::goals.clear();
        TestNav::outcomes.clear();
        TestNav::hold = false;
        TestNav::halts = 0;
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
    const std::string route =
      "0.55;5.18;0.0|4.85;3.74;0.0|7.13;0.10;0.0|5.44;-1.68;0.0|-0.45;-0.63;0.0";

    clear();
    auto tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Missing game exits retreat");
    require(TestNav::goals.empty() && outputs.empty(), "No target before game gate");

    clear();
    set_game(4);
    set_hp(400);
    tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Five waypoints reach base");
    require(TestNav::goals == std::vector<std::string>({route}), "One ordered retreat request");
    require(count("cmd_vel") == 1 && last("cmd_vel") == 0,
      "Only the completed route publishes a stop");
    require(last("auto_aim_switch") == 0 && last("cmd_spin") == 2.2 &&
      last("gimbal_scan_cmd") == 0.5, "Retreat output values");

    clear();
    set_hp(0);
    TestNav::outcomes = {false};
    tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "HP zero after nav failure");
    require(TestNav::goals.size() == 1 && count("cmd_vel") == 1, "HP zero skips retry");

    clear();
    set_hp(400);
    TestNav::outcomes = {false, true};
    tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Failure enters backoff");
    require(TestNav::goals.size() == 1 && count("cmd_vel") == 1, "Stop once on failure");
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Backoff still running");
    require(TestNav::goals.size() == 1 && count("cmd_vel") == 0, "No goal storm or stop flood");
    require(outputs.size() == 3 && last("auto_aim_switch") == 0 &&
      last("cmd_spin") == 2.2 && last("gimbal_scan_cmd") == 0.5, "Heartbeats during backoff");
    std::this_thread::sleep_for(std::chrono::milliseconds(2050));
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Retry reaches base");
    require(TestNav::goals == std::vector<std::string>({
      route, route}),
      "Retry begins at first waypoint");

    clear();
    TestNav::hold = true;
    tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Navigation in progress");
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Navigate heartbeat");
    require(count("cmd_vel") == 0 && count("auto_aim_switch") >= 3 &&
      count("cmd_spin") >= 3 && count("gimbal_scan_cmd") >= 3, "Continuous outputs");
    set_game(5);
    set_hp(0);
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Game end wins over HP zero");
    require(TestNav::goals.size() == 1 && TestNav::halts > 0, "Game end cancels goal");

    clear();
    set_game(4);
    set_hp(400);
    TestNav::outcomes = {false};
    tree = factory.createTree("competition_retreat", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Backoff starts");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Game end cancels backoff");
    require(TestNav::goals.size() == 1, "No retry after game end");

    clear();
    set_game(0);
    std_msgs::msg::Int32 manual;
    manual.data = 0;
    global->set("manual_start", manual);
    auto debug = factory.createTree("competition_test_retreat", local);
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait for manual start");
    require(TestNav::goals.empty() && outputs.empty(), "No control before manual start");
    manual.data = 1;
    global->set("manual_start", manual);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    set_hp(0);
    TestNav::outcomes = {false};
    require(debug.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Manual HP-zero exit");
    require(TestNav::goals.size() == 1 && last("gimbal_scan_cmd") == 0 &&
      last("auto_aim_switch") == 0 && count("cmd_vel") == 2, "Debug stops on HP zero");

    clear();
    set_hp(400);
    manual.data = 0;
    global->set("manual_start", manual);
    debug = factory.createTree("competition_test_retreat", local);
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait for manual restart");
    manual.data = 1;
    global->set("manual_start", manual);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    TestNav::hold = true;
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Manual nav starts");
    manual.data = 0;
    global->set("manual_start", manual);
    require(debug.tickExactlyOnce() == BT::NodeStatus::FAILURE, "Manual stop returns FAILURE");
    require(TestNav::halts > 0 && last("cmd_spin") == 0 &&
      last("gimbal_scan_cmd") == 0 && last("cmd_vel") == 0, "Manual stop cancels and zeros");

    clear();
    set_hp(400);
    manual.data = 1;
    global->set("manual_start", manual);
    debug = factory.createTree("competition_test_retreat", local);
    require(debug.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Manual startup wait");
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    require(debug.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Manual route completes");
    require(TestNav::goals == std::vector<std::string>({
      "-0.45;-0.63;0.0", "5.44;-1.68;0.0", "7.13;0.10;0.0",
      "4.85;3.74;0.0", "0.55;5.18;0.0"}), "Manual route and order");
    require(count("cmd_vel") == 7 && last("cmd_vel") == 0 &&
      last("auto_aim_switch") == 0 && last("cmd_spin") == 0 &&
      last("gimbal_scan_cmd") == 0, "Manual waypoint and final stops");

    std::cout << "Competition retreat: all checks passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
