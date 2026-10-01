/**
 * @file is_map_ready.hpp
 * @brief 声明行为树条件节点 IsMapReadyCondition，对应 XML 标签 IsMapReady。
 *
 * 文件级阅读引导：
 * 1. 本头文件列出类、函数接口和成员变量，具体执行过程写在 is_map_ready.cpp。
 * 2. 先看 public 中的三个函数，再看 private 中保存的 ROS 对象和时间信息。
 * 3. 对照实现文件，按“建立订阅、收到地图、调用 tick 检查”的顺序阅读。
 *
 * 本节点的检查内容：
 * 收到首条地图消息，并且从接收时刻起经过 settle_time 秒，返回 SUCCESS；
 * 其余等待状态返回 FAILURE，外层行为树负责再次调用。
 * timeout 控制等待期间的错误日志；等待与重试由现有行为树继续推进。
 */

#ifndef PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_MAP_READY_HPP_               // 首次包含时进入声明区，后续包含跳过。
#define PB2025_SENTRY_BEHAVIOR__PLUGINS__CONDITION__IS_MAP_READY_HPP_               // 标记本头文件已经包含。

#include <memory>                                                                   
#include <string>                                                                   

#include "behaviortree_cpp/condition_node.h"                                        // 提供行为树条件节点基类 BT::ConditionNode。
#include "behaviortree_ros2/ros_node_params.hpp"                                    // 提供创建 ROS 行为树节点时传入的参数。
#include "nav_msgs/msg/occupancy_grid.hpp"                                          // 提供栅格地图消息 nav_msgs::msg::OccupancyGrid。
#include "rclcpp/rclcpp.hpp"                                                        // 提供 ROS 2 C++ 节点、订阅器、时间和日志接口。

namespace pb2025_sentry_behavior                                                    // 将本包的 C++ 名称放入同名命名空间。
{                                                                                   // 命名空间开始。

/**
 * @class IsMapReadyCondition
 * @brief 订阅地图消息，并在首次接收后的等待时间满足时报告成功。
 *
 * 类级阅读引导：
 * 订阅回调负责写入 map_received_ 和 map_received_at_；
 * tick() 读取这些成员，结合输入端口的时间参数计算返回值。
 * 首次接收状态在本实例中持续保留，后续地图消息直接结束回调。
 * 判断使用消息接收标记和接收后的经过时间，地图宽高用于接收日志。
 */

// 继承behaviortree_ros2的conditionnode条件节点。
// : public 表示当前派生类仍继承基类public，protected, private的对应权限
// 条件节点对系统当前状态进行只读判断，不阻塞。
class IsMapReadyCondition : public BT::ConditionNode                                
{                                                                                   
public:                                                                             

  /**
   * @brief 创建条件节点，保存 ROS 节点并建立地图订阅。
   * @param name 此节点实例在行为树中的名称。
   * @param config 行为树节点配置，包含端口映射及黑板等信息。
   * @param params ROS 集成参数，提供用于订阅、计时和日志的 ROS 节点。
   */

  IsMapReadyCondition(                                                              // 构造函数与类同名，在创建节点实例时执行。
    const std::string & name,                                                       // 引用传递，直接使用外部传入的字符串本身，只传递内存地址
    const BT::NodeConfig & config,                                                  // 通过常量引用接收行为树配置。
    const BT::RosNodeParams & params);                                              // 通过常量引用接收 ROS 参数；函数体位于 cpp。

  /**
   * @brief 声明本节点接受的输入端口及默认值。
   *
   * topic_name 默认 /map，settle_time 默认 5 秒，timeout 默认 120 秒。
   * static 表示框架可以通过类名获取端口定义，然后据此配置节点实例。
   */
  static BT::PortsList providedPorts();                                             // 返回端口列表，供工厂注册节点时使用。

  /**
   * @brief 行为树每次访问本节点时，检查地图接收状态及等待时间。
   *
   * 地图仍在等待接收或等待时间仍在累计时返回 FAILURE；
   * 首次接收后的经过时间达到 settle_time 时返回 SUCCESS。
   * 再次检查的安排由外层控制节点决定。
   */
  BT::NodeStatus tick() override;                                                   // 覆盖基类的 tick 接口，返回行为树节点状态。

private:                                                                            // 类内部保存的对象与状态。

  rclcpp::Node::SharedPtr node_;                                                    // 持有 ROS 节点的共享指针，用于订阅、读时钟和写日志。
  rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr map_sub_;           // 持有地图订阅器。

  bool map_received_ = false;                                                       // 首条地图接收标记，初始值 false，收到后设为 true。
  rclcpp::Time map_received_at_;                                                    // 首次地图回调执行时的 ROS 时间，用于计算接收后的经过时间。
  rclcpp::Time start_time_;                                                         // 构造函数末尾记录的 ROS 时间，用于计算等待接收的经过时间。

};                                                                                  // 类定义结束；分号结束这条类型声明。

}                                                                                   // pb2025_sentry_behavior 命名空间结束。

#endif                                                                              // 与文件开头的 #ifndef 配对，结束头文件重复包含检查。
