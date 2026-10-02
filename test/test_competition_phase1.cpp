#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "behaviortree_cpp/bt_factory.h"
#include "dji_referee_protocol/msg/game_status.hpp"
#include "dji_referee_protocol/msg/robot_performance.hpp"

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
  static std::function<void(const std::string &)> on_success;
  static int failures;
  static int halts;
  static bool hold;

  static BT::PortsList providedPorts()
  {
    return {BT::InputPort<std::string>("goals"), BT::InputPort<std::string>("action_name")};
  }

  BT::NodeStatus onStart() override
  {
    require(getInput<std::string>("action_name").value() == "navigate_through_poses", "Multi-pose Nav2 action");
    goals.push_back(getInput<std::string>("goals").value());
    return hold ? BT::NodeStatus::RUNNING : finish();
  }

  BT::NodeStatus onRunning() override {return hold ? BT::NodeStatus::RUNNING : finish();}
  void onHalted() override {++halts;}

private:
  BT::NodeStatus finish()
  {
    if (failures > 0) {
      --failures;
      return BT::NodeStatus::FAILURE;
    }
    if (on_success) {
      on_success(goals.back());
    }
    return BT::NodeStatus::SUCCESS;
  }
};

std::vector<std::string> TestNav::goals;
std::function<void(const std::string &)> TestNav::on_success;
int TestNav::failures = 0;
int TestNav::halts = 0;
bool TestNav::hold = false;

struct Output
{
  std::string topic;
  double value;
};
}  // namespace

int main(int argc, char ** argv)
{
  try {
    require(argc == 7, "Pass condition/game plugins and phase1/combat/retreat/recover XML paths");
    BT::BehaviorTreeFactory factory;
    factory.registerFromPlugin(argv[1]);
    factory.registerFromPlugin(argv[2]);
    int map_failures = 0;
    int tf_failures = 0;
    int nav2_failures = 0;
    auto ready_after = [](int & failures) {
        if (failures > 0) {
          --failures;
          return BT::NodeStatus::FAILURE;
        }
        return BT::NodeStatus::SUCCESS;
      };
    factory.registerSimpleCondition("IsMapReady", [&](BT::TreeNode &) {
      return ready_after(map_failures);
    }, {BT::InputPort<std::string>("topic_name"), BT::InputPort<double>("settle_time"),
      BT::InputPort<double>("timeout")});
    factory.registerSimpleCondition("IsTfReady", [&](BT::TreeNode &) {
      return ready_after(tf_failures);
    }, {BT::InputPort<std::string>("map_frame"), BT::InputPort<std::string>("base_frame"),
      BT::InputPort<double>("timeout")});
    factory.registerSimpleCondition("IsNav2Ready", [&](BT::TreeNode & node) {
      require(node.getInput<bool>("through_poses").value(), "Multi-pose readiness type");
      require(node.getInput<std::string>("action_name").value() == "navigate_through_poses",
        "Multi-pose readiness name");
      return ready_after(nav2_failures);
    }, {BT::InputPort<std::string>("action_name"), BT::InputPort<double>("timeout"),
      BT::InputPort<bool>("through_poses", false, "Select multi-pose type")});
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
            require(node.getInput<double>("v_y").value() == 0 &&
              node.getInput<double>("v_yaw").value() == 0, "Zero Twist");
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

    for (int i = 3; i <= 6; ++i) {
      factory.registerBehaviorTreeFromFile(argv[i]);
    }
    auto global = BT::Blackboard::create();
    auto local = BT::Blackboard::create(global);
    dji_referee_protocol::msg::GameStatus game;
    dji_referee_protocol::msg::RobotPerformance hp;
    auto set_game = [&](int progress) {
        game.game_progress = progress;
        game.stage_remain_time = 65535;
        global->set("referee_gameStatus", game);
      };
    auto set_hp = [&](int value) {
        hp.current_hp = value;
        global->set("referee_robotPerformance", hp);
      };
    auto last = [&](const std::string & topic) {
        for (auto it = outputs.rbegin(); it != outputs.rend(); ++it) {
          if (it->topic == topic) {
            return it->value;
          }
        }
        throw std::runtime_error("No output on " + topic);
      };
    auto reset = [&] {
        outputs.clear();
        TestNav::goals.clear();
        TestNav::on_success = nullptr;
        TestNav::failures = 0;
        TestNav::halts = 0;
        TestNav::hold = false;
      };
    // 每条字符串对应一次多点请求；比较整串同时检查点数与顺序。
    const std::string forward =
      "-0.45;-0.63;0.0|5.44;-1.68;0.0|7.13;0.10;0.0|4.85;3.74;0.0|0.55;5.18;0.0";
    const std::string retreat =
      "0.55;5.18;0.0|4.85;3.74;0.0|7.13;0.10;0.0|5.44;-1.68;0.0|-0.45;-0.63;0.0";
    const std::vector<std::string> round_route = {forward, retreat};
    auto check_stop = [&] {
        require(last("auto_aim_switch") == 0 && last("cmd_spin") == 0 &&
          last("gimbal_scan_cmd") == 0 && last("cmd_vel") == 0, "Root safe stop");
      };

    reset();
    set_game(0);
    set_hp(400);
    map_failures = tf_failures = nav2_failures = 1;
    auto init_tree = factory.createTree("competition_phase1", local);
    for (int i = 0; i < 3; ++i) {
      require(init_tree.tickExactlyOnce() == BT::NodeStatus::RUNNING,
        "INIT remains RUNNING while a readiness gate fails");
      require(outputs.empty(), "INIT failure must not trigger root safe stop or WAIT_GAME");
      std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
    require(init_tree.tickExactlyOnce() == BT::NodeStatus::RUNNING,
      "INIT proceeds to WAIT_GAME after all gates become ready");
    require(map_failures == 0 && tf_failures == 0 && nav2_failures == 0 &&
      !outputs.empty() && TestNav::goals.empty(), "All readiness gates must be polled");

    reset();
    set_game(0);
    set_hp(400);
    auto tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait for game");
    require(TestNav::goals.empty(), "No goal before game");
    const auto waiting_stops = outputs.size();
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Wait remains RUNNING");
    require(outputs.size() == waiting_stops + 3, "Wait heartbeats without repeated Twist");
    set_game(4);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Reached combat");
    require(TestNav::goals == std::vector<std::string>({forward}), "Forward waypoints, including 65535 game time");
    require(last("cmd_spin") == 7 && last("gimbal_scan_cmd") == 1 &&
      last("auto_aim_switch") == 1, "Combat outputs");
    set_hp(150);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Retreat then recover");
    require(TestNav::goals == round_route, "Combat to retreat route");
    require(last("cmd_spin") == 7 && last("gimbal_scan_cmd") == 0.5 &&
      last("auto_aim_switch") == 0, "Recover outputs");
    set_hp(400);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Recover restarts navigation");
    require(TestNav::goals.size() == round_route.size() + 1 &&
      TestNav::goals[round_route.size()] == forward,
      "Next round starts at first forward waypoint");
    outputs.clear();
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Game end closes whole tree");
    require(outputs.size() == 4 &&
      TestNav::goals.size() == round_route.size() + 1,
      "One safe stop, no next loop");
    check_stop();

    reset();
    set_game(4);
    set_hp(0);
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "HP zero enters recover");
    require(TestNav::goals == std::vector<std::string>({retreat}), "HP zero goes through retreat, not direct recover");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "HP zero game end stops");
    check_stop();

    reset();
    set_game(4);
    set_hp(400);
    TestNav::hold = true;
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "First route active");
    require(last("cmd_spin") == 2.2, "Forward spin speed");
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Continuous route heartbeat");
    for (const auto & output : outputs) {
      require(output.topic != "cmd_vel", "No intermediate stop while following route");
    }
    set_hp(150);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING &&
      TestNav::goals == round_route && TestNav::halts == 1,
      "Low HP halts forward route and starts retreat");
    require(last("cmd_spin") == 2.2 && last("auto_aim_switch") == 0,
      "Retreat spins while skipping combat");
    TestNav::hold = false;
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Retreat completes into recovery");
    require(TestNav::goals == round_route, "Each route is submitted once");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Low HP exit stops");

    reset();
    set_game(4);
    set_hp(400);
    TestNav::on_success = [&](const std::string & goal) {
        if (goal == forward) {
          set_hp(150);
        }
      };
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING,
      "HP drop on final arrival reaches recover");
    require(TestNav::goals == round_route,
      "Completed forward route still enters combat before retreat");
    bool entered_combat = false;
    for (const auto & output : outputs) {
      entered_combat |= output.topic == "auto_aim_switch" && output.value == 1;
    }
    require(entered_combat, "Combat entry is not skipped after final waypoint");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Final arrival case stops");

    reset();
    set_game(4);
    set_hp(400);
    TestNav::failures = 1;
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Failure starts backoff");
    require(TestNav::goals == std::vector<std::string>({forward}), "One failed goal");
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING && TestNav::goals.size() == 1,
      "No immediate resend");
    std::this_thread::sleep_for(std::chrono::milliseconds(2050));
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Retry reaches combat");
    require(TestNav::goals == std::vector<std::string>({
      forward, forward}),
      "Retry restarts full route");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Backoff case stops");

    reset();
    set_game(4);
    set_hp(400);
    TestNav::failures = 1;
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Backoff active");
    set_game(5);
    outputs.clear();
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Game end cancels backoff");
    require(TestNav::goals == std::vector<std::string>({forward}),
      "No retry after game end");
    check_stop();

    reset();
    set_game(4);
    set_hp(400);
    TestNav::hold = true;
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::RUNNING, "Goal active before game end");
    set_game(5);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS, "Game end cancels navigation");
    require(TestNav::halts > 0 && TestNav::goals.size() == 1, "Goal halted, not retried");
    check_stop();

    reset();
    set_game(4);
    set_hp(400);
    TestNav::on_success = [&](const std::string & goal) {
        if (goal == forward) {
          set_game(5);
        }
      };
    tree = factory.createTree("competition_phase1", local);
    require(tree.tickExactlyOnce() == BT::NodeStatus::SUCCESS,
      "Combat FAILURE propagates to root safe stop");
    require(TestNav::goals == std::vector<std::string>({forward}), "Combat FAILURE never retries navigation");
    check_stop();

    std::cout << "Competition phase loop: all checks passed\n";
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "Competition phase loop: " << e.what() << '\n';
    return 1;
  }
}
