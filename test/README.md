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

正式子树仅在低 HP 时关闭自瞄并交给后续 RETREAT；完整主树 `competition_phase1`
现已提供比赛结束时的 spin/扫描/底盘安全收口。验证单独的正式比赛 gate 时，将调试 params 的 target_tree
临时设为 `competition_combat`，先持续发布下列模拟消息，再启动服务与客户端：

```bash
ros2 topic pub -r 5 /referee/common/game_status dji_referee_protocol/msg/GameStatus '{game_progress: 4, stage_remain_time: 420}'
```

停止该模拟发布者后发布 game_progress=5，检查阶段 FAILURE 和自瞄 0；正式入口
不使用 manual_start。服务启动时若尚未收到比赛消息会立即 FAILURE，需在订阅
已收到消息后重新发送 ExecuteTree 请求。正式子树此时没有后续根部停车分支，
此检查只用于隔离的接口测试。Control+C/action cancel 触发的外部 halt 不经过
XML 正常退出分支，输出归零仍属于计划中后续可靠性检查。

## 第三批 RETREAT 验证

正式 `competition_retreat` 使用 `SendNav2ThroughPoses` 一次提交逆序五点：
`(0.55,5.18,0)`、`(4.85,3.74,0)`、`(7.13,0.10,0)`、
`(5.44,-1.68,0)`、`(-0.45,-0.63,0)`。整路线执行上限为 300s。
中间点由 Nav2 连续通过，整条路线成功后由外层发布一次零 Twist。
导航失败时，HP<=0 则发布零 Twist 并结束；其余情况等待 2s 后重试整条路线。
比赛结束会停止当前多点目标。移动阶段持续发布自瞄 0、spin 2.2rad/s、云台 yaw 0.5。

手动 `competition_test_retreat` 保留独立的单点路线，使用
`/navigate_to_pose`，由 `/manual_start` 启停，移动 spin 为 0。
原手动测试继续检查五次单点成功及停止输出。

CTest `competition_retreat` 同时读取正式和手动 XML：正式路线应只有一次多点
请求，手动路线仍有五次单点请求；还检查失败重试、比赛结束和手动停止。

```bash
colcon test --packages-select behavior --ctest-args -R '^competition_retreat$' --output-on-failure
```

## 第四、五批 RECOVER 验证

`competition_recover` 在比赛进行时等待恢复：HP>=400 立即 SUCCESS；
HP>=350 且本轮计时严格超过 30 秒也返回 SUCCESS；其余保持 RUNNING。
比赛结束返回 FAILURE。HP 消息尚未到达时沿用 Python 初值 400；比赛 gate
使用完整 0～65535 剩余时间范围。

进入阶段按 Python 顺序发布自瞄 0、spin 7.0、一次零 Twist 和扫描 0.5。
等待时仅自瞄、spin、扫描按 5Hz tick 重发，零 Twist 不持续发布。
第 5 批的 `WaitForRecovery` 是有状态 RUNNING 节点；其 HP 快照保存运行最小值，
只在跌破该最小值或 HP<=0 时重置计时。这刻意保留 Python 的缺陷，见 BT-017。
`patience_ms=30000` 使用墙上时间，自动测试用 600 ms 缩短等待，不改变正式值。

在已 source ROS 2 和依赖工作空间的环境运行：

```bash
colcon build --packages-select pb2025_sentry_behavior --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_recover$' --output-on-failure
colcon test-result --verbose
```

CTest 加载正式和调试 XML、真实 HP/比赛/手动条件插件，用记录型发布节点检查
399→400、HP=0 等待、比赛结束、入口一次性停车、等待心跳及调试树手动停止。
还直接以 600 ms 配置检查 349/350 门槛、跌破历史最小值、回血后小幅掉血、
HP=0 重置、满血立即出发和 halt 重入。它不验证真实 ROS topic 频率或控制链路。

独立调试树使用 `competition_test_recover`，仅在隔离 ROS_DOMAIN_ID 的仿真或
安全台架运行，不能与其他控制树或 Python 控制器同时运行：

```bash
ros2 launch pb2025_sentry_behavior pb2025_sentry_behavior_launch_new.py params_file:="$(ros2 pkg prefix pb2025_sentry_behavior)/share/pb2025_sentry_behavior/params/sentry_behavior_recover_test.yaml"
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 399}'
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 1}'
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 400}'
```

先将 HP 设为 399，再手动启动；观察 `/cmd_spin`、`/gimbal_scan_cmd`、
`/auto_aim_switch` 约 5Hz 持续输出 7.0、0.5、0，`/cmd_vel_nav2_result`
仅在入口输出一次零速度。HP 设为 400 后调试树停止并全零收口；若仍在等待，
发布 `/manual_start=0` 可手动中止并全零收口。正式子树已接入第 6 批主树；
单独运行正式子树仍无全零出口，主树安全收口待 ROS 验证（BT-013）。
若要验证提前出发路径，重新运行调试树，先持续发布 HP=350，再设置
`/manual_start=1`；保持 HP=350 超过 30 秒，应自动返回 SUCCESS 并全零收口。

## 第六批完整比赛循环验证

正式入口为 `competition_phase1`。前进一次提交顺序五点，撤退一次提交逆序五点，
两阶段使用 `/navigate_through_poses`，移动 spin 为 2.2rad/s。
前进期间每次 tick 检查 HP，HP<=150 会停止前进目标、跳过战斗并进入撤退。
每条路线采用 300s 执行上限；失败后等待 2s，再次提交完整路线。
新目标发送继续使用共用 Action 节点的取消确认处理。

初始化使用 `IsNav2Ready action_name="navigate_through_poses" through_poses="true"`。
`through_poses` 默认 false，已有单点检查继续采用 `NavigateToPose`。
等待开赛、比赛结束时 spin 为 0。战斗、恢复仍发送原有 7rad/s 指令。
Hub 沿用现有控制来源选择：导航开启且速度有效时采用导航输入，速度过期后回到遥控输入。
本轮仅修改行为树侧的连续路线与移动旋转；占点后的持续旋转另行处理。

相关检查：`competition_phase1`、`competition_retreat`、`nav2_ready_types`。正赛测试记录每次多点请求，
检查点序、运行中低血量取消、比赛结束、失败重试和下一轮前进。
就绪检查采用独立 ROS 域 189。

### 实车观察与记录

沿用现有整车、导航和 AMCL 启动流程。启动正式行为树：

```bash
ros2 launch behavior pb2025_sentry_behavior_launch_new.py \
  params_file:=/home/soyo/sentry_ws/src/behavior/params/sentry_behavior_phase1.yaml
```

每条路线开始时应出现 `Sending 5 poses to NavigateThroughPoses`，
完成时出现一次 `NavigateThroughPoses succeeded!`。
观察当前路线剩余点数：

```bash
ros2 action info /navigate_through_poses
ros2 topic echo /navigate_through_poses/_action/feedback \
  nav2_msgs/action/NavigateThroughPoses_FeedbackMessage \
  --field feedback.number_of_poses_remaining --csv
```

独立终端记录实际底盘输入和输出，使用新文件夹保存每次测试：

```bash
mkdir -p /home/soyo/sentry_ws/log
ros2 bag record --include-hidden-topics \
  -o "/home/soyo/sentry_ws/log/continuous_route_$(date +%Y%m%d_%H%M%S)" \
  /cmd_spin /cmd_vel /chassis_command \
  /navigate_through_poses/_action/feedback
```

无遮挡路线中间点应连续通过；移动 `/cmd_spin` 为 2.2，最终
`/chassis_command.spin_speed` 应体现该转速。现场同步记录遥控档位，检查中间点是否仍有
速度间隙和遥控小陀螺接管。导航断流继续采用现有 Hub 的遥控输入处理。
现场测试由操作人员随时准备遥控急停；急停后的零输出单独记录。
取消命令针对新的多点 Action：

```bash
ros2 service call /navigate_through_poses/_action/cancel_goal \
  action_msgs/srv/CancelGoal '{}'
```

正式树会重试被取消的导航；整轮结束仍使用 `game_progress=5`。
终端文本若使用 awk 过滤，采用 `awk -W interactive` 及时记录每行输出。
实车停止后依次结束行为树、记录程序和导航，保留终端日志及 bag。
