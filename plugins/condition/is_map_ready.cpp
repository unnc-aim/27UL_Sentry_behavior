/**
 * @file is_map_ready.cpp
 * @brief 实现 IsMapReadyCondition，并将它注册为 XML 中的 IsMapReady 插件。
 *
 * 文件级阅读引导：
 * 1. 构造函数取得 ROS 节点，读取地图话题名称，建立订阅并记录开始时间。
 * 2. ROS 执行器收到地图后调用订阅回调，回调保存首次接收标记和接收时间。
 * 3. providedPorts() 告诉行为树工厂：本节点接受哪些输入，以及各输入的默认值。
 * 4. tick() 每次被行为树访问时进行一次检查，返回 FAILURE 或 SUCCESS。
 * 5. 文件末尾的注册宏，将 C++ 类与 XML 名称 IsMapReady 关联。
 *
 * 时间与返回值：
 * 所有经过时间均使用 node_->now() 所属的 ROS 时钟计算。
 * 收到首条地图并经过 settle_time 秒后成功；其他等待状态返回 FAILURE。
 * timeout 用于选择等待日志的级别，后续检查由外层行为树的重试节点驱动。
 * 当前 competition_phase1 主树在初始化检查失败后等待 500 ms，再次检查。
 */

#include "behavior/plugins/condition/is_map_ready.hpp"                              // 引入本类声明，供下方定义成员函数。

namespace behavior                                                                  // 与头文件中的命名空间对应。
{                                                                                   // 命名空间开始，下方成员函数属于此空间中的类。

/**
 * @brief 构造节点并建立地图订阅。
 *
 * 类与函数的对应关系：
 * IsMapReadyCondition:: 表示正在实现这个类的成员；
 * 后面的同名 IsMapReadyCondition 表示构造函数。
 *
 * params.nh 是 ROS 节点的弱指针，lock() 尝试取得有效的共享指针。
 * 订阅建立后，ROS 执行器负责分派回调；tick() 读取回调保存的状态。
 */
IsMapReadyCondition::IsMapReadyCondition(                                           // 定义构造函数。
  const std::string & name,                                                         // 接收行为树节点实例名称。
  const BT::NodeConfig & config,                                                    // 接收输入端口、黑板等行为树配置。
  const BT::RosNodeParams & params)                                                 // 接收 ROS 集成参数。
: BT::ConditionNode(name, config),                                                  // 初始化父类，让行为树框架保存节点名称与配置。
  node_(params.nh.lock())                                                           // 将有效 ROS 节点保存为共享指针；取得失败时得到空指针。
{                                                                                   // 构造函数体开始，此时父类与成员初始化已经执行。
  if (!node_) {                                                                     // 取得的 ROS 节点指针为空时，进入错误处理。
    throw BT::RuntimeError("IsMapReady: the ROS node went out of scope");           // 抛出创建节点的异常。
  }                                                                                 // ROS 节点有效性检查结束。

  std::string topic_name = "/map";                                                  // 为本次创建订阅准备默认话题名称。
  getInput("topic_name", topic_name);                                               // 从行为树输入端口读取话题名称，写入局部变量。

  auto qos = rclcpp::QoS(rclcpp::KeepLast(1))                                       // 订阅历史深度为 1。
               .transient_local()                                                   // 请求兼容发布端保留的历史地图，供较晚创建的订阅器接收。
               .reliable();                                                         // 采用可靠传输。

  /**
   * @brief 建立地图订阅，回调只保存首次接收的状态。
   *
   * [this] 让匿名函数可以访问当前对象的成员变量。
   * msg 是收到的地图消息的共享指针，通过按值传递参与此次回调。
   * 后续消息会直接返回，因此首次接收时间在本实例中持续保留。
   */
  map_sub_ = node_->create_subscription<nav_msgs::msg::OccupancyGrid>(              // 创建并保存地图订阅器。
    topic_name,                                                                     // 指定地图话题。
    qos,                                                                            // 采用上方配置的通信方式。
    [this](const nav_msgs::msg::OccupancyGrid::SharedPtr msg) {                     // 定义地图消息的匿名回调函数。

      if (map_received_) {                                                          // 首次地图接收标记已经为 true 时，执行下面的提前返回。
        return;                                                                     // 结束此次回调，保留已经记录的首次接收时间。
      }                                                                             // 后续消息的处理分支结束。

      map_received_ = true;                                                         // 记录“已收到首条地图”。
      map_received_at_ = node_->now();                                              // 保存本地接收时刻，作为 settle_time 的计时起点。

      RCLCPP_INFO(                                                                  // 记录首条地图的尺寸，便于观察订阅结果。
        node_->get_logger(),                                                        // 使用当前 ROS 节点的日志器。
        "IsMapReady: map %ux%u received on latched topic",                          // 两个 %u 分别对应地图宽、高。
        msg->info.width,                                                            // 栅格地图宽度，单位为格。
        msg->info.height);                                                          // 栅格地图高度，单位为格。

    });                                                                             // 结束匿名函数和 create_subscription 调用，将订阅器保存到 map_sub_。

  start_time_ = node_->now();                                                       // 记录构造阶段的 ROS 时间，供等待接收的 timeout 判断使用。
}                                                                                   // 构造函数结束。

/**
 * @brief 向行为树工厂提供输入端口定义。
 *
 * 每个 InputPort 的三个参数依次为名称、默认值、说明文字。
 * XML 中的同名属性通过这些端口传入，例如 settle_time="5.0"。
 * 返回的列表描述节点接受的输入，实际读取发生在构造函数或 tick() 中。
 */
BT::PortsList IsMapReadyCondition::providedPorts()                                  // 定义头文件中声明的静态成员函数。
{                                                                                   // 端口定义函数开始。
  return {                                                                          // 使用初始化列表构造并返回 BT::PortsList。

    BT::InputPort<std::string>(                                                     // 字符串端口：指定地图话题。
      "topic_name",                                                                 // XML 中对应的属性名称。
      "/map",                                                                       // 默认地图话题。
      "Latched OccupancyGrid topic to wait for"),                                   // 端口说明文字。

    BT::InputPort<double>(                                                          // 浮点端口：首次收到地图后的等待时间。
      "settle_time",                                                                // XML 中对应的属性名称。
      5.0,                                                                          // 默认等待 5 秒。
      "Seconds to keep waiting after the first map"),                               // 端口说明文字。

    BT::InputPort<double>(                                                          // 浮点端口：等待接收时采用错误日志的时间。
      "timeout",                                                                    // XML 中对应的属性名称。
      120.0,                                                                        // 默认等待超过 120 秒后记录错误日志。
      "Seconds before the missing map is logged as an error"),                      // 端口说明文字。

  };                                                                                // 端口列表和 return 语句结束。
}                                                                                   // providedPorts() 函数结束。

/**
 * @brief 执行一次地图就绪检查。
 *
 * 按顺序阅读三个分支：
 * 1. 等待首条地图：记录等待日志并返回 FAILURE。
 * 2. 已收到地图，经过时间小于 settle_time：继续返回 FAILURE。
 * 3. 已收到地图，经过时间达到 settle_time：返回 SUCCESS。
 *
 * 此函数通过时间差进行检查，调用后立即返回当前结果。
 * 外层控制节点安排后续 tick，地图订阅回调由 ROS 执行器分派。
 */
BT::NodeStatus IsMapReadyCondition::tick()                                          // 实现行为树更新时调用的成员函数。
{                                                                                   // tick() 函数开始。
  double settle_time = 5.0;                                                         // 初始化接收后等待时间，单位为秒。
  double timeout = 120.0;                                                           // 初始化接收等待的日志时间，单位为秒。

  getInput("settle_time", settle_time);                                             // 每次 tick 都尝试读取当前输入端口值。
  getInput("timeout", timeout);                                                     // 每次 tick 都尝试更新错误日志的等待时间。

  /*
   * 第一阶段：首条地图仍在等待接收，使用 start_time_ 计算经过时间。
   */
  if (!map_received_) {                                                             // 接收标记为 false 时进入此分支。

    if (timeout > 0.0 && (node_->now() - start_time_).seconds() > timeout) {        // 正数 timeout 已经过期。
      RCLCPP_ERROR_THROTTLE(                                                        // 对此日志调用位置限制输出频率。
        node_->get_logger(),                                                        // 使用当前节点的日志器。
        *node_->get_clock(),                                                        // 使用当前节点的 ROS 时钟。
        5000,                                                                       // 此调用位置的日志输出间隔为 5000 ms。
        "IsMapReady: no /map message after %.1fs, still waiting (INIT not ready)",  // 错误日志格式。
        timeout);                                                                   // 将配置的等待秒数填入 %.1f，保留一位小数。

    } else {                                                                        // timeout 为零或负数，或等待时间仍在配置范围内时，采用普通信息日志。
      RCLCPP_INFO_THROTTLE(                                                         // 对普通等待信息限制输出频率。
        node_->get_logger(),                                                        // 使用当前节点的日志器。
        *node_->get_clock(),                                                        // 使用当前节点的 ROS 时钟。
        2000,                                                                       // 此调用位置的日志输出间隔为 2000 ms。
        "IsMapReady: waiting for /map ...");                                        // 提示仍在等待地图。
    }                                                                               // 等待日志的选择结束。

    return BT::NodeStatus::FAILURE;                                                 // 向行为树报告本次检查失败，由外层决定下一次检查。
  }                                                                                 // 首条地图的接收等待分支结束。

  /*
   * 第二阶段：已经收到首条地图，改用 map_received_at_ 计算接收后的经过时间。
   */
  const double settled_for = (node_->now() - map_received_at_).seconds();           // 将 ROS 时间差转为秒。

  if (settled_for < settle_time) {                                                  // 经过时间仍小于所需等待时间时，继续等待。
    RCLCPP_INFO_THROTTLE(                                                           // 记录等待进度，并限制此调用位置的输出频率。
      node_->get_logger(),                                                          // 使用当前节点的日志器。
      *node_->get_clock(),                                                          // 使用当前节点的 ROS 时钟。
      1000,                                                                         // 此调用位置的日志输出间隔为 1000 ms。
      "IsMapReady: map received, settling %.1f/%.1fs ...",                          // 显示已经经过与要求等待的秒数。
      settled_for,                                                                  // 第一个 %.1f：已经经过的秒数。
      settle_time);                                                                 // 第二个 %.1f：要求等待的秒数。

    return BT::NodeStatus::FAILURE;                                                 // 当前等待仍在进行，本次条件检查返回失败。
  }                                                                                 // 接收后的等待分支结束。

  /*
   * 第三阶段：已经收到地图，且经过时间达到 settle_time。
   */
  RCLCPP_INFO_ONCE(                                                                 // 此调用位置在进程中输出一次成功提示。
    node_->get_logger(),                                                            // 使用当前节点的日志器。
    "IsMapReady: map ready");                                                       // 地图接收与等待时间检查成功。

  return BT::NodeStatus::SUCCESS;                                                   // 通知父节点本次检查成功，父节点可以推进后续步骤。
}                                                                                   // tick() 函数结束。

}                                                                                   // behavior 命名空间结束。

/**
 * @brief 导出 ROS 行为树插件的注册入口。
 *
 * 加载插件库时，框架通过此入口将 C++ 类登记为 XML 节点 IsMapReady。
 * CMakeLists.txt 中的 is_map_ready 共享库包含本文件。
 */
#include "behaviortree_ros2/plugins.hpp"                                            // 提供用于导出插件注册入口的宏。

CreateRosNodePlugin(                                                                // 导出插件注册入口。
  behavior::IsMapReadyCondition,                                                    // 注册的完整 C++ 类名。
  "IsMapReady");                                                                    // 行为树 XML 使用的标签名称。
