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

`competition_retreat` 是正式撤退子树。只在比赛进行中工作：依次导航
`(6.0,5.25,0)`、`(6.0,0,0)`、`(0,0,0)`，每个目标有 60s Timeout。
每个成功航点后发送一次零 Twist；全部到达后再发一次并返回 SUCCESS。
导航失败时检查 HP，若 HP<=0，发零 Twist 后返回 SUCCESS，交给后续 RECOVER；
否则发一次零 Twist，等待 2s，再从第一航点重试。比赛 gate 每 tick 检查，
比赛结束时取消正在运行的目标或退避并返回 FAILURE，不开始下一次重试。
HP 缺少消息时沿用 Python 初值 400；比赛 gate 采用 `game_progress=4` 和
0～65535 全时域，保留 BT-012 对第一阶段时间窗不一致的后续跟踪。

进入阶段时发送自瞄 0、spin 0、云台扫描 0.5，随后在导航和 2s 退避期间
按 5Hz tick 持续重发三种输出；零 Twist 只在航点成功、导航失败或 HP=0
分支各自指定位置发送。一次成功进基地后的额外零 Twist 复刻 Python 的
`_ph_retreat()`；导航目标拒绝/失败时 Python 可能在 `_navigate_to()` 和阶段
处理函数各停车一次，当前 XML 只在退避分支发一次，详见 BT-015。

在已 source ROS 2 和依赖工作空间的环境运行：

```bash
colcon build --packages-select pb2025_sentry_behavior --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_retreat$' --output-on-failure
colcon test-result --verbose
```

CTest 加载实际正式/调试 XML 及 HP、比赛、手动条件插件，以记录型 Nav2 动作
模拟成功、失败和长时间 RUNNING，并检查四种出口。它不替代真实 Nav2
取消确认、60s 时钟和 ROS topic 频率测试。

独立调试入口 `competition_test_retreat` 用 `/manual_start=1` 触发，不依赖
GameStatus；HP=0 仍需等当前导航失败后才进入 RECOVER。可在隔离
ROS_DOMAIN_ID 的仿真环境用以下 params 运行并观察 `/cmd_spin`、
`/gimbal_scan_cmd`、`/auto_aim_switch`、`/cmd_vel_nav2_result`：

```bash
ros2 launch pb2025_sentry_behavior pb2025_sentry_behavior_launch_new.py params_file:="$(ros2 pkg prefix pb2025_sentry_behavior)/share/pb2025_sentry_behavior/params/sentry_behavior_retreat_test.yaml"
ros2 topic pub --once /referee/common/robot_performance dji_referee_protocol/msg/RobotPerformance '{current_hp: 400}'
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 1}'
ros2 topic pub --once /manual_start std_msgs/msg/Int32 '{data: 0}'
```

前两条消息应在启动目标前发送；最后一条用于手动停止。调试树成功到达、
HP=0 撤退失败或手动停止时，都发布全零停止输出后结束本轮；它不会自动进入
RECOVER，也不会自动重启。真实目标由 Nav2 接收，因此只在仿真或安全隔离的
台架运行。单独运行正式 `competition_retreat` 时，它的比赛结束 FAILURE 出口
仍没有全零输出；完整主树 `competition_phase1` 的根部安全收口已在第 6 批添加，
实际 ROS topic 验证仍按 BT-013 跟踪。

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

默认 `target_tree=competition_phase1` 现连接 INIT、WAIT_GAME、NAVIGATE、COMBAT、
RETREAT、RECOVER，并在 RECOVER 成功后重新从前进路线第一航点开始。前进路线
每个航点开始前检查 HP；当前目标执行期间 HP 下降不会立即取消目标，符合 Python。
导航失败会停车并等待 2 秒，从第一航点重试；HP<=150 时跳过 COMBAT 进入
RETREAT，HP=0 仍按 Python 的条件优先级先进入 RETREAT（BT-018）。

WAIT_GAME 等待期间返回 RUNNING，入口只发一次零 Twist；云台、spin、自瞄
按 tick 重发。比赛结束或阶段 FAILURE 时，根部一次性发布自瞄 0、spin 0、
云台零速度和底盘零 Twist，然后结束整棵树。此收口不覆盖外部 action cancel、
进程异常或 Control+C；这些路径仍按既有问题记录处理。

在已 source ROS 2 和依赖工作空间的环境执行：

```bash
colcon build --packages-select pb2025_sentry_behavior --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_phase1$' --output-on-failure
colcon test-result --verbose
```

CTest 加载四份正式 XML、真实 HP/热量/比赛条件插件，以记录型导航及发布节点
模拟完整循环。检查 0～65535 比赛时间、低 HP 航点分流、HP=0 先撤退、
2 秒导航退避、RECOVER 后再次前进、比赛结束取消目标与四项归零，
以及 COMBAT 的 FAILURE 不会被导航重试吞掉。CTest 不验证 ROS topic 序列化、
真实 Nav2 取消和硬件控制链路。本机未运行 ROS 构建或 CTest；通过前不要上车。
