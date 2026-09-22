# 第一批 HP / 热量节点验证

`IsHpLow` 的 `hp_port` 默认读取全局 `{@referee_robotPerformance}`，字段为
`RobotPerformance.current_hp`。`threshold` 默认为 150，`HP <= threshold` 返回
SUCCESS，否则 FAILURE。可选 bool 输出 `hp_low` 用于观察结果。

`TrackHeat` 的 `heat_port` 默认读取全局 `{@referee_robotHeat}`，字段为
`RobotHeat.shooter_17mm_barrel_heat`。`stop_heat=240`、`resume_heat=50`，要求
`0 <= resume_heat < stop_heat <= 65535`。int 输出 `aim_allowed` 为 0/1，可直接
连接 `PublishAutoAim.value`。运行期间始终返回 RUNNING；halt 清除锁存并将输出
置 0；重入时重新从未过热状态计算。消费者应放在 Parallel 中 TrackHeat 后面，
不能把它放在普通 Sequence 的发布节点之前，否则 RUNNING 会阻止后续发布。

两节点不订阅、不发布 ROS topic。消息尚未到达时按 Python 使用 HP=400、热量=0；
消息到达后读取黑板保存的最新消息，不增加过期检测。XML 消息端口应使用上述默认
映射或对应消息类型的黑板项；测试使用真实消息类型，无弹药消息依赖。

## 构建和自动检查（ROS 2 工作机）

在已经 source ROS 和依赖工作空间的终端，在包含此包的工作空间运行：

```bash
colcon build --packages-select pb2025_sentry_behavior --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_conditions$' --output-on-failure
colcon test-result --verbose
```

测试通过真实工厂动态加载插件，检查全局黑板映射、无消息初值、HP 151/150/0、
热量 239/240/100/51/50、自定义阈值、非法参数、同 tick 输出顺序和父节点 halt 后重入。

## 独立 ROS 调试入口

构建并 source install/setup.bash 后运行（空 namespace）：

```bash
ros2 launch pb2025_sentry_behavior pb2025_sentry_behavior_launch_new.py params_file:="$(ros2 pkg prefix pb2025_sentry_behavior)/share/pb2025_sentry_behavior/params/sentry_behavior_conditions_test.yaml"
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 1}'
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 151}'
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 150}'
ros2 topic pub --once /referee/common/robot_heat dji_referee_protocol/msg/RobotHeat '{shooter_17mm_barrel_heat: 240}'
ros2 topic pub --once /referee/common/robot_heat dji_referee_protocol/msg/RobotHeat '{shooter_17mm_barrel_heat: 51}'
ros2 topic pub --once /referee/common/robot_heat dji_referee_protocol/msg/RobotHeat '{shooter_17mm_barrel_heat: 50}'
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 0}'
```

逐条执行并留出至少一秒观察。PrintRefereeStatus 每秒打印输入，状态日志显示
IsHpLow 的 SUCCESS/FAILURE；TrackHeat 在启动和热量迟滞切换时打印 aim_allowed。
热量输出和 halt 重入同时由上面的自动检查验证。manual_start=0
中止运行中的 TrackHeat；再次置 1 重新初始化。该入口没有控制发布节点和导航目标，
只执行条件观察。使用隔离 ROS_DOMAIN_ID，避免模拟裁判消息与真实裁判发布者混用。

当前正式入口仍为 competition_phase1。此调试树的 ForceSuccess 仅用于手动反复
启动测试，不作为正式比赛根节点模板。
