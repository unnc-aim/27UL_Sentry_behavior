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

## 第二批 COMBAT 验证

`competition_combat` 为正式阶段子树：比赛进行且 HP > 150 时 RUNNING；
HP <= 150 时显式发布自瞄 0 后 SUCCESS；比赛结束/消息缺失时发布自瞄 0 后
FAILURE。比赛状态优先于 HP。复用 IsGameStatus 并将剩余时间范围设为
0～65535，覆盖消息 uint16 全域，与 Python 只判断 game_progress==4 一致。
HP/热量消息缺失仍分别按 400/0 处理。

`competition_combat_active` 是两个入口共用的内部子树，调用方必须提供阶段 gate。
入口依次发布零 Twist、spin 7.0、扫描 1.0、自瞄 1；随后按当前热量计算迟滞，
同 tick 发布结果。入口已过热时会先出现 1 再出现 0，与 Python 入口先开自瞄一致。
稳定运行时每 tick 重发 spin/扫描/自瞄，Twist 不重发。首 tick 包含入口和循环
两组输出，不应要求首 tick 每个 topic 恰好只有一条消息。

自动回归（先执行上面的 colcon build）：

```bash
colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_(conditions|combat)$' --output-on-failure
colcon test-result --verbose
```

COMBAT 测试加载真实 HP、热量、比赛和手动条件插件及实际 XML，发布节点替换为
记录型节点，验证一次性停车、连续重发、热量同 tick 生效、HP/比赛出口优先级、
halt 重入和调试入口两个出口。它不验证 ROS 消息序列化、实际发布频率或 Hub 接收。

独立调试树会实际发布控制命令。使用隔离 ROS_DOMAIN_ID 的仿真/台架环境，
所有终端使用同一个 domain；不要同时运行其他控制树或 Python 控制器。

```bash
ros2 launch pb2025_sentry_behavior pb2025_sentry_behavior_launch_new.py params_file:="$(ros2 pkg prefix pb2025_sentry_behavior)/share/pb2025_sentry_behavior/params/sentry_behavior_combat_test.yaml"
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 151}'
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 1}'
```

调试入口用 manual_start 替代比赛 gate，不需要比赛消息；PrintRefereeStatus 每秒
打印一次。另开终端用 `ros2 topic echo` 检查以下接口，用 `ros2 topic hz` 测稳定
运行频率（目标 5 Hz，实际间隔需小于 0.5s）：

| Topic | 消息类型 | 稳定运行输出 |
| --- | --- | --- |
| `/cmd_vel_nav2_result` | `geometry_msgs/msg/Twist` | 入口一次全零，运行中不重发；launch 将 XML 的 cmd_vel 重映射到此 |
| `/cmd_spin` | `example_interfaces/msg/Float32` | 7.0，每 tick |
| `/gimbal_scan_cmd` | `pb_rm_interfaces/msg/GimbalCmd` | VELOCITY，yaw=1.0、pitch=0，每 tick |
| `/auto_aim_switch` | `std_msgs/msg/Int32` | 迟滞决定 0/1，每 tick |

按第一批示例发布热量 `239 → 240 → 51 → 50`，检查自瞄 `1 → 0 → 0 → 1`。
发布 HP=150，调试树应返回 SUCCESS；重新启动调试入口并恢复 HP=151，发布
manual_start=0，应返回 FAILURE。两个出口都会由调试包装发布全零停止输出，
包括额外的一次零 Twist；调试树不会自动重启或发送撤退导航目标。

正式子树仅在低 HP 时关闭自瞄并交给后续 RETREAT；比赛结束时由后续主树安全
分支完成 spin/扫描/底盘停止。验证正式比赛 gate 时，将调试 params 的 target_tree
临时设为 `competition_combat`，先持续发布下列模拟消息，再启动服务与客户端：

```bash
ros2 topic pub -r 5 /referee/common/game_status dji_referee_protocol/msg/GameStatus '{game_progress: 4, stage_remain_time: 420}'
```

停止该模拟发布者后发布 game_progress=5，检查阶段 FAILURE 和自瞄 0；正式入口
不使用 manual_start。服务启动时若尚未收到比赛消息会立即 FAILURE，需在订阅
已收到消息后重新发送 ExecuteTree 请求。正式子树此时没有后续根部停车分支，
此检查只用于隔离的接口测试。Control+C/action cancel 触发的外部 halt 不经过
XML 正常退出分支，输出归零仍属于计划中后续可靠性检查。
