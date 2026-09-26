# 问题跟踪

用于记录行为树迁移、联调和运行过程中发现的待修复问题。

## 状态说明

- `待处理`：问题已确认，尚未开始修复。
- `处理中`：正在修改或验证。
- `待验证`：已修改，等待构建、仿真或实车验证。
- `待测试`：风险或边界行为尚未在隔离环境实测；测试前不记为已确认故障。
- `已解决`：验收条件全部满足。
- `不处理`：确认接受现状，并记录原因。

## BT-001：单航点缺少执行超时

- 状态：`待验证`
- 优先级：`高`
- 发现日期：2026-09-20
- 涉及文件：`behavior_trees/competition_phase1.xml`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`

### 问题

Python 状态机在 Nav2 接受单个航点后，最多等待 60 秒。超时后会取消当前目标，并将本次导航判定为失败。

当前 XML 和 C++ 只限制发送目标后等待 Nav2 接受目标的时间，没有限制目标被接受后等待最终结果的时间。若 Nav2 一直不返回 `SUCCEEDED`、`ABORTED` 或 `CANCELED`，`SendNav2Goal` 将持续返回 `RUNNING`。

### 影响

- 当前航点可能永久占用导航流程。
- 后续航点不会执行。
- `RetryUntilSuccessful` 收不到 `FAILURE`，因此无法触发路线重试。
- 比赛仍在进行时，没有处理导航卡死的退出路径。
- Nav2 可能持续发布速度、振荡或重复局部规划。

比赛状态结束或行为树被外部取消时，现有 halt 路径仍会尝试取消 Nav2 目标；本问题主要影响比赛进行期间的导航卡死。

### 建议处理

为每个 `SendNav2Goal` 增加 60 秒结果等待上限。超时后：

1. 取消当前 Nav2 目标。
2. 由 `RosActionNode::halt()` 等待取消和结果返回。
3. 显式发布零速度。
4. 返回 `FAILURE`，交给路线重试逻辑处理。

超时值应保留为可调整参数，以便根据仿真和实车正常航行时间校准。

### 验收条件

- [ ] Nav2 正常完成时，三个航点依次执行。
- [x] 单个航点超过配置时间后，当前目标被取消。
- [x] 超时后底盘收到显式零速度命令。
- [x] 超时返回 `FAILURE`，不会永久停留在 `RUNNING`。
- [ ] 后续重试不会与上一个未取消完成的目标重叠。
- [ ] 比赛结束时仍能及时取消当前目标。
- [x] 超时值可以通过 XML 调整。

### 解决记录

- 修复提交：`47234d6`
- 验证环境：
- 验证结果：XML 结构检查通过；待仿真或实车验证 Nav2 超时取消行为。
- 备注：每个 `SendNav2Goal` 外包裹 `<Timeout msec="60000">`；超时中止动作节点时，`RosActionNode` 自动取消当前目标。路线失败后先发布零速度，再进入 2 秒重试等待。

---

## BT-002：导航重试无 2s 退避

- 状态：`待验证`
- 优先级：`低`
- 发现日期：2026-09-21
- 涉及文件：`behavior_trees/competition_phase1.xml`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`（`_ph_navigate` 失败后 `_spin_ros(2.0)` 再重试）

### 问题

`RetryUntilSuccessful num_attempts="-1"` 包裹整条前进航线，航点失败后立即重发目标。Python 在导航失败与重试之间有 2 秒等待。

### 影响

- 目标不可达时形成 goal 风暴（Nav2 立刻 abort → 立刻重发）。

### 建议处理

路线失败后先发布零速度，再等待 2 秒后重新执行整条前进路线。

### 验收条件

- [x] 失败重试前有 XML 配置的 2 秒等待时间。

### 解决记录

- 修复提交：`47234d6`
- 验证环境：
- 验证结果：XML 结构检查通过；待仿真或实车验证失败重试节奏。
- 备注：使用内置 `<Sleep msec="2000">`，没有新增自定义重试节点。

---

## BT-003：WAIT_GAME 缺少 300s 裁判超时

- 状态：`待处理`
- 优先级：`低`
- 发现日期：2026-09-21
- 涉及文件：`behavior_trees/competition_phase1.xml`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`（`REFEREE_TIMEOUT = 300.0`）

### 问题

XML 注释只声明了 INIT 超时改为"记录不退出"，未提 `REFEREE_TIMEOUT`。现在裁判系统不上线就永远等待。

### 影响

- 裁判系统不上线时静默死等，无超时退出路径。

### 建议处理

确认是否有意去掉；若保留等待，需记录决策原因。

### 验收条件

- [ ] 明确该差异是刻意保留还是遗漏，并记录结论。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-004：比赛结束后云台扫描未停止

- 状态：`待处理`
- 优先级：`低`
- 发现日期：2026-09-21
- 涉及文件：`behavior_trees/competition_phase1.xml`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`（`finally` 中 `_scan_off()`）

### 问题

行为树在比赛结束经 loop-back 回到 wait_game 后仍以 0.5 rad/s 扫描。Python 在 `finally` 里停扫描。

### 影响

- 比赛结束后云台持续扫描。
- 将来删除 loop-back 时，若退出路径不补发零速 GimbalCmd，树没有任何"停下扫描"的出口。

### 建议处理

删除 loop-back 时，退出路径必须补一个零速 GimbalCmd。

### 验收条件

- [ ] 树退出后 `/gimbal_scan_cmd` 停止或变为零速。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-005：5s settle 的位置与 Python 不同

- 状态：`待处理`
- 优先级：`低`
- 发现日期：2026-09-21
- 涉及文件：`behavior_trees/competition_phase1.xml`、`plugins/condition/is_map_ready.cpp`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`（三门全过后的 costmap 等待）

### 问题

Python 是三个就绪门全部通过后等待 5s 让 costmap 填充；行为树里 5s settle 在 map 门内部（TF/server 门之前）。

### 影响

- 效果近似，实际影响可忽略。

### 建议处理

可不处理；如需对齐，将 settle 移到三门之后。

### 验收条件

- [ ] 确认现状可接受，或已对齐到三门之后。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-006：超时/退避同步后的三个小差异

- 状态：`待处理`
- 优先级：`低`
- 发现日期：2026-09-22
- 涉及文件：`behavior_trees/competition_phase1.xml`、`plugins/action/send_nav2_goal.cpp`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`

### 问题

60s 单航点超时与"零速 + 2s 退避 + 整线重试"已同步到行为树（`Timeout` 包裹 `SendNav2Goal`、`Fallback` 退避分支）。核对后存在三个可接受的小差异：

1. 计时起点不同：BT 的 60s 从节点开始 tick 起算（含 goal 发送/接受往返）；Python 在 goal 被接受后才开始计时。差值约一个 RTT（server_timeout 默认 ~1s）。
2. cancel 等待上限不同：BT ~1s（RosActionNode 默认 server_timeout）vs Python `_cancel_nav` 的 5s。最坏情况 cancel 未确认就重发新 goal，Nav2 单 goal 策略会抢占旧 goal，实际自愈。
3. 前进航点成功后原先不发零速：Python 每个航点成功后都发一次 `_stop_chassis()`；第 6 批已在前进路线每个成功航点后补一次零 Twist，并在整个前进路线成功后再补一次阶段停车。

前进和撤退路线现在都在每个成功航点后发送零 Twist（第 6 批提交 `d844d08`）；该数量仍待 ROS 与 Nav2 实测。

### 影响

- 差异 1、2 在 60s 预算内可忽略。
- 差异 3 实车如有航点间残余抖动可观察 `/cmd_vel` 是否归零。

### 建议处理

差异 1、2 暂不调整；前进成功路径的零 Twist 已在第 6 批补齐。实车联调时仍需观察航点切换瞬间底盘行为。

### 验收条件

- [ ] 实车或仿真中航点切换瞬间 `/cmd_vel` 归零、无残余移动。
- [ ] 确认三个差异均可接受，并记录结论。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-007：第一批 HP / 热量节点等待 ROS 构建与运行验证

- 状态：`待验证`
- 发现日期：2026-09-22
- 涉及文件：`plugins/condition/competition_conditions.cpp`、对应头文件、`CMakeLists.txt`、`behavior_trees/competition_test_conditions.xml`、`params/sentry_behavior_conditions_test.yaml`、`test/test_competition_conditions.cpp`。
- 实现：`IsHpLow` 独立判断 `RobotPerformance.current_hp <= threshold`；`TrackHeat` 使用 `RobotHeat.shooter_17mm_barrel_heat` 和 240/50 迟滞，运行期间返回 RUNNING，输出整数 `aim_allowed`，halt 时清锁存、输出 0。
- 接口：沿用全局黑板 `referee_robotPerformance` / `referee_robotHeat`；不新增订阅，不依赖 AllowedShoot。消息未到达时复刻 Python 初值 HP=400、热量=0，不增加消息失效策略。
- 调试：独立入口 `competition_test_conditions`，manual_start=1 启动，0 halt；不发送运动、自瞄或 Nav2 命令。默认 competition_phase1 未改动。
- 已验证：XML 语法、黑板键与现有服务器一致、注册名和 params 入口一致、diff 空白检查。
- 未验证：当前机器执行 cmake 返回 command not found，且未找到 colcon / ROS 构建环境。不能将结构检查视为真实 C++ 编译或插件运行通过。
- 回归检查已加入 CTest：动态加载真实插件，检查初值、HP 边界、热量迟滞、自定义/非法阈值、父节点 halt 和重入、同 tick 输出顺序。
- [ ] ROS 构建通过。
- [ ] `competition_conditions` CTest 通过。
- [ ] 按 `test/README.md` 启动调试树并核对消息及返回状态。
- 提交：待验证后按批次提交；已有 `.gitignore` 改动保持原状。

---

## BT-008：非法热量阈值会中止整棵树的执行 action

- 状态：`待验证`
- 优先级：`高`（第五轮确认配置错误后的安全输出风险；正式 XML 阈值合法）
- 发现日期：2026-09-22
- 涉及文件：`plugins/condition/competition_conditions.cpp`、`BehaviorTree.ROS2/behaviortree_ros2/src/tree_execution_server.cpp`

### 问题

`TrackHeat` 在首次 tick 时经 `onStart()` 调用 `onRunning()`。当 `stop_heat <= resume_heat` 或阈值越界时，节点会抛出 `BT::RuntimeError`。`TreeExecutionServer` 捕获该异常后会直接 abort 当前 `ExecuteTree` action；该异常不是节点的 `FAILURE` 返回值，因此 XML 中的 `Fallback` 无法接管，也不会执行正常退出分支。

这是配置错误的预期处理方式，不属于当前迁移逻辑缺陷；需要避免后续把它误认为可由行为树分支恢复的运行时状态。

### 影响

- 阈值配置错误会终止整棵树，而不是只结束 `TrackHeat`。
- 不能依赖根部停车或关闭输出分支处理该异常。

第五轮补充：`competition_combat_active` 先发布底盘零速度、`spin=7.0`、云台扫描速度和自瞄 1，随后才启动 `TrackHeat`（`behavior_trees/competition_combat.xml:6-15`）。若阈值异常在此处抛出，`TreeExecutionServer` 直接 abort，不执行根部四项归零；先前输出可能残留。代码路径已静态确认，正式 XML 使用合法常量，实际输出仍待 ROS 故障注入验证。安全收口另由 BT-013 跟踪，测试归 BT-022 的待测-06。

同族路径：`WaitForRecovery::onStart` 对 `enough_hp/full_hp/patience_ms` 的非法组合同样抛出 `BT::RuntimeError`（`plugins/condition/competition_conditions.cpp` 第五批新增校验），异常同样穿透根部 Fallback 直接 abort，行为与 `TrackHeat` 一致，不另开编号。

### 建议处理

保留当前严格校验；在 ROS 环境验证 action 的 abort 状态和错误信息。部署参数修改后应先运行条件节点测试。

### 验收条件

- [ ] 非法阈值测试能够观察到 `ExecuteTree` action 被 abort，并带有明确错误信息。
- [ ] 合法阈值下节点正常进入 RUNNING。

### 解决记录

- 修复提交：无需修复，属于配置保护。
- 验证环境：
- 验证结果：
- 备注：

---

## BT-009：Parallel 中返回 SUCCESS 的持续输出节点只会执行一次

- 状态：`待验证`
- 优先级：`高`
- 发现日期：2026-09-22
- 预计处理批次：第 2 批 COMBAT 集成
- 实施记录（2026-09-22）：`competition_combat_active` 使用 RUNNING 热量分支和持续输出分支，消费者由 `KeepRunningUntilFailure` 每 tick 驱动；Parallel 的 success_count=1 仅让低 HP 正常完成分支结束阶段。已添加基于实际 XML 的 CTest，尚未运行；实际 5 Hz 发布及 <0.5s 间隔待 ROS 验证。

### 问题

BT.CPP 的 `Parallel` 会记录已经返回 SUCCESS 的子节点，并在本轮后续 tick 中跳过它。即使 `success_count="-1"` 且 `TrackHeat` 持续 RUNNING，使 Parallel 本身保持 RUNNING，直接放入其中的 `PrintRefereeStatus`、`PublishAutoAim` 等 SUCCESS 节点仍只会执行一次。

当前第一批测试使用 `KeepRunningUntilFailure` 包裹观察节点，能够持续 tick；第 2 批不能省略同类持续执行结构。

### 影响

- 若自瞄发布节点只执行一次，消息会在接口要求的 0.5s 心跳期限后失效。
- 热量迟滞发生切换时，新的 `aim_allowed` 值可能不会被发布。

### 建议处理

第 2 批将所有需要持续发布的节点放入能够保持 RUNNING 且每 tick 重发的结构中；不要把普通 SUCCESS 发布节点直接作为 Parallel 子节点使用。

### 验收条件

- [ ] COMBAT 期间 `/auto_aim_switch` 的发布间隔始终小于 0.5s。
- [ ] 热量跨越 240/50 阈值时，在同一 tick 发布新的 0/1 值。
- [ ] 单个发布节点返回 SUCCESS 不会停止后续重发。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-010：TrackHeat halt 时清零黑板不等于关闭自瞄

- 状态：`待验证`
- 优先级：`高`
- 发现日期：2026-09-22
- 涉及文件：`plugins/condition/competition_conditions.cpp`
- 预计处理批次：第 2 批 COMBAT 集成
- 实施记录（2026-09-22）：低 HP 分支显式发布自瞄 0 后返回 SUCCESS；正式子树比赛 gate 失败时显式发布 0 并保持 FAILURE。独立调试入口的手动停止路径也显式发布 0，并将 spin/扫描/底盘归零。CTest 已加入对应消息调用检查，真实 ROS 消息仍待验证。

### 问题

`TrackHeat::onHalted()` 当前将黑板输出 `aim_allowed` 设为 0。该操作只更新行为树黑板，不会向 `/auto_aim_switch` 发布消息；同时退出阶段时消费者也会被 halt，不能保证它还能读取并发布这个 0。

`aim_allowed` 是后续发布节点的控制输入，halt 清零可作为内部状态复位，但不能作为关闭自瞄的执行机制。

### 影响

- 如果 COMBAT 退出路径没有显式发布 0，自瞄端可能继续保持上一个有效状态，直到自身超时。
- 只检查黑板值会形成误判，无法证明 ROS 接口已经收到关闭命令。

### 建议处理

第 2 批在 COMBAT 的正常流程退出路径显式执行 `PublishAutoAim value="0"`，并以 ROS topic 实际消息作为验收依据。外部 halt（例如 action cancel/Control+C）不会执行 XML 的退出分支，按迁移计划留待后续可靠性处理，不包含在本批已实现范围。

### 验收条件

- [ ] HP 触发撤退、比赛结束及调试手动停止的流程退出均显式发布自瞄 0（外部 halt 不在本批范围）。
- [ ] `/auto_aim_switch` 能观察到退出命令，不能只以黑板 `aim_allowed=0` 判定成功。
- [ ] halt 后重新进入 COMBAT 时，热量锁存与输出重新按当前热量初始化。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-011：第二批 COMBAT 等待构建与接口运行验证

- 状态：`待验证`
- 实现提交：`7c0adb8 feat(behavior): migrate combat phase`（同时实现 BT-009、BT-010 的正常流程处理，运行验收仍待完成）。
- 发现日期：2026-09-22
- 涉及文件：`behavior_trees/competition_combat.xml`、`behavior_trees/competition_test_combat.xml`、`params/sentry_behavior_combat_test.yaml`、`test/test_competition_combat.cpp`、`test/README.md`、`CMakeLists.txt`。
- 实现：一次性入口零 Twist；spin=7.0、scan=1.0、自瞄按 240/50 迟滞每 tick 重发。HP<=150 返回 SUCCESS；比赛结束优先返回 FAILURE。所有正常流程出口显式关闭自瞄，调试包装另有停止输出。
- 等价说明：复用 IsGameStatus 并使用 0～65535 剩余时间范围，覆盖 uint16 字段全域，保持 Python 仅检查 game_progress==4 的语义；不改第一阶段的比赛条件。按计划以 5 Hz tick 重发，Python 的 spin/自瞄定时重发约 2 Hz。入口先开自瞄，再在同 tick 按当前热量调整；不悄悄更改入口已过热时先开再关的行为。
- 接口：复用现有发布节点及消息类型。现有 launch 将 `cmd_vel` 重映射为 `/cmd_vel_nav2_result`，实际停车消息应在后者观察。
- 调试包装与比赛流程区别：手动 gate 替代比赛 gate，HP 退出或手动停止后执行全零停止输出并结束一轮；不连接尚未迁移的 RETREAT。正式主树入口仍为 competition_phase1。
- 验证限制：本机仍无 CMake、colcon/ROS；已添加真实 XML + 条件插件 + 记录型发布节点的 CTest，但尚未执行。XML/静态检查不能代替插件加载、实际 ROS topic 或实车验证。外部 halt 清理按原计划留待后续可靠性处理。
- [ ] 第 1、2 批 CTest 与 ROS 构建通过。
- [ ] 真实接口输出类型、值、发布间隔和零 Twist 次数符合 `test/README.md`。
- [ ] HP=151/150、热量 239/240/51/50、比赛结束、手动停止及重入验证通过。

---

## BT-012：比赛阶段的 IsGameStatus 时间窗不一致

- 状态：`已改 XML，待 ROS 验证`
- 优先级：`中`
- 发现日期：2026-09-22
- 涉及文件：`behavior_trees/competition_phase1.xml`、`behavior_trees/competition_combat.xml`
- 预计处理批次：第 6 批完整比赛循环集成

### 问题

第一阶段的 `IsGameStart` 和 `IsGameRunning` 使用 `max_remain_time="420"`，COMBAT 使用 `65535`。当 `game_progress=4` 且 `stage_remain_time > 420` 时，NAVIGATE gate 判定比赛未进行，COMBAT gate 则判定正常。

正赛剩余时间通常不超过 420 秒，因此当前比赛流程一般不会触发；使用更长时间的训练赛配置时会出现同一主树内 gate 语义不一致。

### 建议处理

第 6 批集成时将比赛循环中仅用于判断 `game_progress == 4` 的 gate 统一为 uint16 全范围 `0～65535`，保持 Python `_game_on()` 的语义。若某处确实需要时间限制，应单独命名并说明用途。

第 6 批已将 WAIT_GAME 和 NAVIGATE 的 gate 以及根部 gate 改为 `0～65535`；COMBAT、RETREAT、RECOVER 原本已用该范围。新增 `competition_phase1` CTest 使用 `stage_remain_time=65535`，但本机未运行 ROS 构建及 CTest。
实现提交：`d844d08 feat(behavior): integrate competition phase loop`。

### 验收条件

- [ ] WAIT_GAME、NAVIGATE、COMBAT、RETREAT、RECOVER 的比赛状态 gate 使用一致语义。
- [ ] `stage_remain_time` 为 420、421、65535 且 `game_progress=4` 时均判定比赛进行中。
- [ ] 非进行阶段仍返回 FAILURE。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：

---

## BT-013：完整比赛循环尚未实现统一的安全停止输出

- 状态：`主树已实现，待 ROS 与实际 topic 验证`
- 优先级：`高`
- 发现日期：2026-09-22
- 涉及文件：`behavior_trees/competition_combat.xml`、`behavior_trees/competition_retreat.xml`、`behavior_trees/competition_recover.xml`、后续第 6 批主树
- 预计处理批次：第 6 批完整比赛循环集成

### 问题

正式 `competition_combat` 在比赛结束的 FAILURE 路径只显式发布自瞄 0。spin 仍可能保持 7.0，云台扫描仍可能保持 1.0，底盘停止依赖尚未实现的根部安全收口分支。

这是第二批按计划推迟的集成工作。当前独立调试树 `competition_test_combat` 已在 HP 完成和手动停止时发布全零输出，但正式 COMBAT 子树不能代替完整主树的安全退出逻辑。

第 3 批正式 RETREAT 子树在比赛结束时也直接返回 FAILURE，未单独发布完整停止输出。根部安全收口完成前，不能把该子树的退出状态当作“已归零”；调试请使用具有全零退出分支的 `competition_test_retreat`。

第 4 批正式 RECOVER 子树同样在比赛结束时直接返回 FAILURE，没有显式发布零输出；`test_competition_recover.cpp` 目前断言该 tick 没有发布。进入 RECOVER 后最近一次 spin 指令为 7.0，COMBAT 在比赛结束时也可能留下 spin 7.0（及扫描 1.0），因此 RECOVER 不是唯一的高风险阶段。独立调试树有自己的全零出口，但不能替代正式树收口。

实际底盘是否持续旋转不能只由 XML 推断：所查本地 `sentry_ws/src/universal_controller` 的 Hub 在收到比赛结束状态后会屏蔽外部 spin；但 `/cmd_spin` 只缓存数值、没有超时清零。若状态不同步或之后重新进入比赛阶段，残留 7.0 仍可能被再次使用。该 Hub 保护不能视为行为树已经安全停车，实车链路需验证。

### 建议处理

第 6 批在根部 FAILURE 收口路径一次性发布：自瞄 0、spin 0、云台零速度和底盘零 Twist，并确保该分支执行后结束整棵树。将此项设为正式比赛主循环上车前的硬验收条件；在此之前，正式阶段子树只用于短时、隔离的接口检查，常规独立调试使用各自带全零出口的测试树。不要把 `competition_test_recover_stop` 当作正式主树必须加载的调试文件依赖。

第 6 批已在 `competition_phase1` 的根部 `Fallback` 添加上述四项一次性输出；`Repeat` 子树失败时由此收口，不再使用旧的 `KeepRunningUntilFailure/ForceSuccess` 无限循环根节点。新增集成 CTest 检查输出记录和不再发送目标；本机未运行 ROS 构建、CTest 或真实 topic 验证。单独运行 COMBAT/RETREAT/RECOVER 正式子树仍没有该根部保护。
实现提交：`d844d08 feat(behavior): integrate competition phase loop`。

### 第三轮补充：结束比赛与新目标的竞态（高，待验证）

裁判比赛结束消息由订阅回调写入全局黑板，行为树在独立线程逐节点 tick（`src/pb2025_sentry_behavior_server.cpp:41-48,163-166`；`/Users/lingdu/Documents/sentry_ws_2/src/BehaviorTree.ROS2/behaviortree_ros2/src/tree_execution_server.cpp:154-163,230-231`）。

若 RECOVER 达到出发条件并重入 NAVIGATE，比赛结束更新恰好落在 NAVIGATE 的 `IsGameStatus` 检查之后、首个 `SendNav2Goal` 的 `async_send_goal` 之前（`behavior_trees/competition_phase1.xml:76-118`），当前 XML 没有在发送前再次检查状态，可能在结束消息已写入后仍发出一个前进目标。下一 tick 的根部比赛门预计会停止、取消目标并发布四项零值；这不能证明从未发出结束后的目标，也不能仅凭节点 halt 证明 Nav2 服务端已确认取消。

现有 `test/test_competition_phase1.cpp` 未在该窗口注入状态更新；本机没有 ROS/CTest 环境，尚无运行复现。此项补充本问题的“结束后不发新目标”验收，不记为已确认故障。

### 第五轮补充：非正常出口与安全发布失败（高，待 ROS 验证）

- 外部取消只调用 `haltTree()`，不会 tick 根部安全分支；Control+C 导致 `rclcpp::ok()` 为 false 时，执行循环直接退出，连该取消路径的 `haltTree()` 也未调用（`/Users/lingdu/Documents/sentry_ws_2/src/BehaviorTree.ROS2/behaviortree_ros2/src/tree_execution_server.cpp:203-289`）。COMBAT 中最后发布的自瞄 1、`spin=7.0` 等输出可能残留；`TrackHeat::onHalted()` 清零黑板不能代替 topic 归零（BT-010）。
- 条件或发布端口抛异常时，执行服务直接 abort，根部 `Fallback` 无法处理；`TrackHeat` 的具体触发顺序见 BT-008。
- 根部安全分支本身若有发布节点返回 `FAILURE`，内部 `Sequence` 会跳过后续零指令，外层 `ForceSuccess` 仍返回 `SUCCESS`（`behavior_trees/competition_phase1.xml:155-165`）。当前根部端口是有效字面值，此路径需故障注入；执行结果误报另见 BT-019。

上述三项均不能用正常比赛结束时的四项归零测试代替；对应待测-05 至待测-07 归档在 BT-022。

### 验收条件

- [ ] 任一比赛阶段因比赛结束返回 FAILURE 后，根部发布全部安全停止输出。
- [ ] `/auto_aim_switch=0`、`/cmd_spin=0`、云台零速度和底盘零 Twist 均可在实际 topic 观察到。
- [ ] 安全停止后不会重新进入比赛循环或继续发送控制心跳。
- [ ] 用可控回调在 NAVIGATE 比赛门通过后、首个 goal 发送前注入比赛结束；记录 goal 序列与四项输出，并用假 Nav2 服务端确认若已发送目标，其取消得到服务端确认。
- [ ] 在实际使用的 Hub 上验证：比赛结束时不会继续旋转，且下次比赛开始前残留的 `/cmd_spin=7.0` 已由安全收口清除。
- [ ] 外部取消、Control+C 和节点异常后，实际 topic 不残留自瞄开启、非零 spin、云台速度或底盘运动；分别核对 ExecuteTree 终态。
- [ ] 根部任一安全发布失败时，不把未完成的停车报告为成功；仍尝试其余可发送的零指令，并暴露失败原因。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：BT-004 已单独记录云台扫描停止；本项跟踪第 6 批完整根部收口。

---

## BT-014：第一批条件调试树缺少真实 XML 文件回归加载

- 状态：`待处理`
- 优先级：`低`
- 发现日期：2026-09-22
- 涉及文件：`behavior_trees/competition_test_conditions.xml`、`test/test_competition_conditions.cpp`、`CMakeLists.txt`

### 问题

第一批测试会动态加载真实 `competition_conditions` 插件，但 HP 和热量树由测试代码中的内嵌 XML 创建，没有加载 `behavior_trees/competition_test_conditions.xml` 文件。因此节点本身已有边界和生命周期检查，实际调试树文件的节点组合、端口名和树 ID 尚未进入自动回归。

第二批 COMBAT 测试已采用加载实际 XML 文件的方式，可复用同一模式补齐覆盖。

### 建议处理

后续测试整理时，让第一批测试额外注册并创建 `competition_test_conditions.xml` 中的树，至少完成一次 tick 和 halt；不改变条件节点逻辑。

### 验收条件

- [ ] CTest 从仓库中的 `competition_test_conditions.xml` 创建真实调试树。
- [ ] `/manual_start` 启动、HP/热量输入、停止和 halt 路径至少执行一次。
- [ ] XML 的节点 ID、端口映射或组合结构出错时测试会失败。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：`IsHpLow` 裸用时已有 `output_ports.count("hp_low")` 保护，不登记空绑定写入缺陷。

---

## BT-015：第三批 RETREAT 等待构建及导航接口验证

- 状态：`待验证`
- 实现提交：`de0c245 feat(behavior): migrate retreat phase`。
- 发现日期：2026-09-26
- 涉及文件：`behavior_trees/competition_retreat.xml`、`behavior_trees/competition_test_retreat.xml`、`params/sentry_behavior_retreat_test.yaml`、`test/test_competition_retreat.cpp`、`CMakeLists.txt`、`test/README.md`
- 实现：三个撤退航点、逐点 60s Timeout；导航失败后检查 HP=0，正常失败则停车并等待 2s，从第一航点重试；比赛状态门位于重试器外，比赛结束返回 FAILURE；成功到达或失败且 HP=0 返回 SUCCESS。运行期间自瞄 0、spin 0、扫描 0.5 按 tick 重发。
- 调试：`competition_test_retreat` 使用 `/manual_start` gate，成功和手动停止会显式输出全零并结束。正式 `competition_retreat` 尚未连接第 6 批根部安全收口，仍按 BT-013 跟踪。
- 与 Python 的微小差异：`_navigate_to()` 对已接受目标的失败结果会先停车一次，`_ph_retreat()` 随后再停车一次；目标被拒绝时也可能双发。当前 XML 失败分支只发一次零 Twist，仍满足 2s 退避和底盘停下的核心语义。实车/仿真如要求严格复刻零速消息次数，可单独补齐；不得将本项隐匿或混同导航失败重试节奏。
- 同时比赛结束与 HP=0 的状态优先级不同（低，非第 4/5 批阻塞项）：Python 在撤退导航返回失败后先检查 HP<=0，会短暂进入 RECOVER，先发布一套入口输出（含 spin=7.0），再因比赛已结束退出并由 `finally` 归零；当前正式 BT 在撤退重试器外先检查比赛状态，直接返回 FAILURE，第四批 RECOVER 的比赛 gate 也不会在 game-off 时补发入口输出。这是同一根因的阶段轨迹差异，不另开问题；BT-016 交叉引用。只有第 6 批根部安全收口实现并验证后，才能说两者最终安全输出等价；在此之前不得标作“已解决”。
- 测试覆盖缺口（中，验证项）：`test_competition_retreat.cpp` 的 `SendNav2Goal` 桩把 `goal` 声明成 `std::string`，只验证 XML 中的目标文字和顺序；真实节点使用 `PoseStamped` 并通过 `custom_types.hpp` 将三字段 `x;y;yaw` 转成位置和四元数。当前 CTest 不覆盖该解析路径；本机又无法运行 ROS 集成测试。此缺口不会改变已写的 XML 控制流程，但不能据此声称真实 Nav2 目标接口已通过验证。
- 限制：CTest 已编写但本机无 CMake、colcon/ROS；尚未验证插件运行、真实 Nav2 取消、60s 超时或 ROS 心跳间隔。
- 静态检查：两个 XML 文件通过 `xmllint --noout`；已核对树 ID、params 的 `target_tree`、现有发布/导航插件端口以及 `git diff --check`。此结果不等于 CTest 通过。
- [ ] ROS 构建以及第一至三批 CTest 通过。
- [ ] 真实 Nav2 成功、失败、取消和超时行为按阶段契约工作。
- [ ] 撤退重试从第一航点开始，2s 内没有 goal 风暴。
- [ ] HP=0 与比赛结束分别触发 SUCCESS/FAILURE，后者不再发下一目标。
- [ ] 调试树手动启动/停止及三种控制 topic 的频率与退出全零输出通过 ROS 接口检查。
- [ ] 判断失败时一次零 Twist 与 Python 双次零 Twist 的差异是否接受。
- [ ] 明确同时比赛结束且 HP=0 时优先 FAILURE 的偏差是否保留；第 6 批验证最终安全输出，不把阶段轨迹误称为等价。
- [ ] 在 ROS 环境验证三个真实 `PoseStamped` 目标的 x/y/yaw 转换与 Nav2 接收；如补充自动测试，需经过实际 `convertFromString<PoseStamped>`，不能只复用字符串桩。

---

## BT-016：第四批 RECOVER 满血路径待 ROS 验证

- 状态：`待验证`
- 优先级：`中`
- 实现提交：`6b90f7e feat(behavior): migrate full-hp recovery`
- 发现日期：2026-09-26
- 涉及文件：`behavior_trees/competition_recover.xml`、`behavior_trees/competition_test_recover.xml`、`params/sentry_behavior_recover_test.yaml`、`plugins/condition/competition_conditions.cpp`、`test/test_competition_recover.cpp`

### 当前实现与限制

第四批当时仅实现 HP>=400 出发；第五批已将判断门替换为
`WaitForRecovery`，加入 HP>=350 且等待超过 30 秒的提前出发路径。
等待期间返回 RUNNING；进入时一次零 Twist，等待期间按 tick 重发自瞄 0、
spin 7.0、扫描 0.5。Python 历史最小 HP 快照缺陷按计划保留，另见 BT-017。
正式阶段比赛结束返回 FAILURE，安全输出交第 6 批根部收口（BT-013）。
独立调试树用 `/manual_start` 启动，退出时显式全零。

本批审查提醒（不另开重复条目）：

- 低：比赛已结束时直接调用正式 RECOVER，game gate 返回 FAILURE、无入口输出；Python 在 BT-015 所述 RETREAT 失败且 HP=0 的组合下会先短暂发布 RECOVER 入口输出。此差异归 BT-015，最终安全状态仍依赖 BT-013。
- 低：第四批的 `recover_hp_low` 黑板项曾只写不读，值是未取反的 `HP<=399`（“未满血”）；`current_hp` 为 `uint16`，阈值 399 与 `HP>=400` 完全等价。第五批替换判断门时已移除这项诊断输出，没有把它误当作“允许出发”。
- 低：现有 CTest 已检查等待 tick 的三种心跳，但 HP 满血的出口 tick 只断言没有重复零 Twist，没有断言该 tick 的心跳。阶段已结束，不把它当作持续重发缺陷；如需固定出口发布语义，再补测试。
- 低：若 HP 话题从未到达，第四批的 `IsHpLow` 和第五批的 `WaitForRecovery` 均沿用 Python 初值 400，独立 RECOVER 子树会当帧 SUCCESS。正常完整路径先因低 HP 离开 COMBAT，通常不会在从未收到 HP 时进入 RECOVER；收到过消息后断线则保留最近值，不会自动回到 400。`test_competition_recover.cpp` 的满血入口用例显式设置了 HP=400，不能视为“从未收到消息”测试；`test/README.md` 已说明默认值。
- 低：正式 `competition_recover.xml` 中 `WaitForRecovery` 配置为 `full_hp=400`、`enough_hp=350`、`patience_ms=30000`，但现有计时 CTest 使用内联树的 `350/600`，未直接断言正式 XML 的 `enough_hp` 和 `patience_ms`。正式值若误改，现有测试仍可能通过。后续宜在现有测试中检查已加载正式树的端口值，不必等待真实 30 秒；当前先记录，不改测试。
- 低：`WaitForRecovery::onStart()` 校验三个阈值端口，`onRunning()` 则每 tick 用 `.value()` 直接取值。当前正式 XML 使用固定字面值且端口有默认值，暂无可触发的运行中读取失败；将来若改为动态黑板绑定且绑定失效，异常可能中止 `ExecuteTree`，而不是返回节点 `FAILURE`，参见 BT-008。届时按配置是否允许运行中变化，选择缓存已校验值或每 tick 显式校验；当前先记录，不改代码。

第 5 批实现约束（中，待 ROS 验证）：当前 `Parallel failure_count="1"` 下，新恢复节点在“尚未满足出发条件”时必须保持 RUNNING，不能直接返回 FAILURE，否则整个阶段会被当作比赛结束/致命失败。代码已按此结构实现，并在进入时初始化 HP 快照和计时、halt/重入时重置；判断顺序复刻 Python：先 HP>=400，再 HP>=350 且等待超过 30 秒，之后才处理历史最小 HP 快照，最后 HP<=0 重置计时。静态审查和新增 CTest 不能替代尚未运行的 ROS 验证；历史最小值缺陷仍按 BT-017 跟踪。

与 Python 的微小时间差异：Python 进入 RECOVER 后先执行一次最长 0.5 秒的
`spin_once`，再检查 HP；当前行为树在首 tick 即可检查并返回满血 SUCCESS。
满血出发条件与输出值相同，但边界上的首次判断时刻不完全一致；目前按非致命
迁移差异记录，不在第四批加入人为延迟。

当前 macOS 无 ROS 2/CMake/colcon，CTest 已编写但未运行；XML 语法和端口
静态检查不能替代实际插件加载、5Hz 发布或 ROS 控制链路验证。

### 验收条件

- [ ] ROS 构建及 `competition_recover` CTest 通过。
- [ ] HP 399→400、HP=0 等待、比赛结束的返回值正确。
- [ ] 一次性零 Twist 和自瞄/spin/扫描心跳在 ROS 接口上通过频率检查。
- [ ] 手动调试树启动/停止与退出全零输出在隔离环境验证。
- [ ] 第 6 批根部安全收口覆盖正式 RECOVER 的比赛结束 FAILURE。
- [ ] 第 5 批的新恢复节点等待时持续 RUNNING，halt/重入和 Python 判断顺序按上述约束验证。
- [ ] 在现有 CTest 中固定正式 XML 的 `WaitForRecovery` 参数值，避免只验证内联短计时树。

计时源决策（第七批批次 4 复核）：`WaitForRecovery` 使用 `std::chrono::system_clock`
墙钟，与 Python `time.time()` 等价；NTP 前跳会使 30 秒耐心提前到期，回拨则延长。
Python 存在相同风险，属等价保留而非新缺陷。若日后改用 `steady_clock` 消除该
风险，属于"比 Python 更安全"的主动偏离，须单独提交并记录，不得混入其他改动。

---

## BT-017：RECOVER 的历史最小 HP 快照可能使提前出发过早

- 状态：`待处理（迁移阶段有意保留）`
- 优先级：`低`
- 等价迁移提交：`a4900bc feat(condition): add recovery readiness logic`
- 发现日期：2026-09-26
- 涉及文件：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`、`plugins/condition/competition_conditions.cpp`、`test/test_competition_recover.cpp`
- 预计处理：完整 Python→BT 迁移和验收后，单独决定是否修正

### 问题

Python 的 `hp_snapshot` 只在当前 HP 低于已有快照时更新，实际保存的是本轮 RECOVER 的历史最小 HP。回血到较高值后再小幅掉血，只要仍高于该历史最小值，就不会重置 30 秒计时。例如从 HP=300 开始，20 秒时回血到 380，25 秒时降到 370，约 30 秒时仍可能按原计时提前出发；若按“每次掉血重置”语义则不会。第五批 `WaitForRecovery` 刻意复刻了 Python 的判断顺序和该缺陷，没有静默修复。

### 影响与验证

这是原 Python 行为的非致命迁移等价问题，不是第五批新引入的回归。若将来决定调整恢复策略，应在独立改动中明确新语义并验证实车/仿真影响。新增 CTest 使用短计时检查“回血后下降但仍高于历史最小值”不会重置；本机无 ROS 构建环境，测试尚未运行。

### 验收条件

- [ ] ROS 环境运行第五批 CTest，确认当前迁移实现与 Python 行为一致。
- [ ] 迁移完成后决定是否改为“每次 HP 下降重置计时”；若修正，单独提交并记录新旧行为与验证结果。

### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：

---

## BT-018：NAVIGATE 中 HP=0 的直达 RECOVER 分支不可达

- 状态：`已按原优先级集成，待 ROS 验证`
- 优先级：`低`
- 发现日期：2026-09-26
- 涉及文件：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`、`behavior_trees/competition_phase1.xml`

### 问题

Python 的 `_ph_navigate()` 在导航未完成后先判断 `HP <= 150` 并进入 RETREAT，随后才判断 `HP <= 0` 并进入 RECOVER。后一个条件已被前一个条件覆盖，因此 HP=0 仍先进入 RETREAT，不能在迁移时改成直达 RECOVER。

### 影响与处理

这是原 Python 的不可达分支，不是行为树新引入的故障。第 6 批 XML 已让 HP<=150（含 HP=0）优先选择 RETREAT；新增集成 CTest 检查 HP=0 时首先发送撤退航点，而非直接等待 RECOVER。迁移完成后再单独决定是否修正；本机未运行 ROS 构建及 CTest。
实现提交：`d844d08 feat(behavior): integrate competition phase loop`。

### 验收条件

- [ ] 集成测试证明 NAVIGATE 中 HP=0 先进入 RETREAT，而非直达 RECOVER。
- [ ] 第 7 批复核是否保留此原有语义。

---

## BT-019：根部安全收口不区分比赛结束与其他阶段失败

- 状态：`待测试`（退出原因混淆暂不更改；安全发布失败待验证）
- 优先级：`高`（第五轮发现安全分支发布失败时的误报；原退出原因混淆为低）
- 发现日期：2026-09-26
- 涉及文件：`behavior_trees/competition_phase1.xml`、`plugins/action/pub_gimbal_velocity.cpp`

### 现状与影响

主树用 `Fallback` 将主分支的所有 `FAILURE` 交给同一套安全停止输出，再由 `ForceSuccess` 以 `SUCCESS` 结束。因此树结果本身不能区分比赛结束与阶段故障。INIT 未就绪时现在经异步重试保持 `RUNNING`，WAIT_GAME 正常等待也保持 `RUNNING`，但不能据此断言主分支只会因比赛结束失败：例如云台速度节点遇到无效范围时 `setMessage()` 返回 `false`，发布节点会返回 `FAILURE`，而非抛异常。真正抛出的异常不会经过这条 `Fallback`，另参见 BT-008。

第五轮补充：若根部四项安全发布中任一项返回 `FAILURE`，内部 `Sequence` 停在该项，后续零指令不发送；`ForceSuccess` 仍使树返回 `SUCCESS`（`behavior_trees/competition_phase1.xml:155-165`）。这会把未完成的安全停止误报为成功。正式根树使用有效字面端口，目前只有结构证据，需注入发布失败验证；输出安全性由 BT-013 跟踪，测试归 BT-022 待测-07。

### 后续处理

目前保留统一安全停止契约，不改阶段返回值。若后续需要从执行结果区分比赛结束和故障，应另行增加退出原因诊断，并分别检查 `FAILURE` 与异常路径；不把 `SUCCESS` 直接解释成“比赛正常结束”。

---

## BT-020：INIT 就绪超时只报警，仍会无限等待

- 状态：`已记录，暂不更改`
- 优先级：`低`
- 发现日期：2026-09-26
- 涉及文件：`behavior_trees/competition_phase1.xml`、`plugins/condition/is_map_ready.cpp`、`plugins/condition/is_tf_ready.cpp`、`plugins/condition/is_nav2_ready.cpp`
- 参考实现：`/Users/lingdu/Documents/sentry_ws_2/scripts/competition_controller.py`（`_ph_init`）

### 差异与影响

Python 等待地图和 TF 共用约 120 秒就绪期限，Nav2 action server 另有 30 秒期限；超时后 `_ph_init` 返回 `False`，主循环退出并执行安全关闭。当前三个 BT 就绪节点超过各自 `timeout` 后只记录错误、继续返回 `FAILURE`；INIT 外层的无限重试每 500 ms 再检查一次，不会因超时退出。地图、TF 或 Nav2 永久不可用时，整树会一直停在 INIT。这是已知迁移差异，不是本次新增的 WAIT_GAME 开赛前 TF 一次性复检路径；BT-003 曾顺带提及，但此前没有单独跟踪。

### 后续处理

本阶段仅记录。迁移完成后再决定是否恢复 Python 的超时退出及相应安全收口，并在 ROS 环境核对永久未就绪情形。

---

## BT-021：开赛前 TF 复检缺少集成回归用例

- 状态：`已记录，暂不修改测试`
- 优先级：`低`
- 发现日期：2026-09-26
- 涉及文件：`behavior_trees/competition_phase1.xml`、`test/test_competition_phase1.cpp`

### 问题与影响

第 6 批集成测试现已覆盖 INIT 就绪门先返回 `FAILURE`、整树继续保持 `RUNNING` 的情形，但尚未覆盖 INIT 通过后、WAIT_GAME 等待期间 TF 失效、随后比赛开始的转换。正式 XML 在开赛前增加了 `IsTfReady` 一次性复检；若以后误删或改坏该门，现有测试无法检出，可能在 TF 不可用时仍发出前进导航目标。

### 后续处理

目前只记录，不修改测试或行为逻辑。后续增加集成用例：先让 INIT 通过并进入 WAIT_GAME，再令 TF 桩返回 `FAILURE` 并把比赛状态切到 IN_GAME；断言没有发送 Nav2 goal，且根部执行安全停止。保留 TF 正常时能进入 NAVIGATE 的正向用例。该复检只发生在开赛转换时，不代表导航过程中持续检查 TF。

---

## BT-022：待测试事项归档

- 状态：`待测试`
- 优先级：`高`（以待测项中的最高风险为准）
- 发现日期：2026-09-27
- 范围：第七批第四轮及后续审查的待测风险；这里记录测试场景和结果，原问题仍由关联的 BT 条目跟踪。

后续待测试事项继续在本条下递增编号。每项记录触发顺序、受影响接口、通过判据、实际结果和关联 BT 编号；实测失败后再更新对应问题或另立缺陷，不把未运行的推断写成已确认故障。

### 待测-01：迟到接受或取消超时后旧 Nav2 目标仍活动

- 风险：`高`；关联：BT-001、BT-006；状态：`待测试`。
- 测试：隔离假 Nav2 分别拒绝 goal、延迟 goal 接受超过 `ros_plugins_timeout=1000 ms`、延迟取消确认或结果；覆盖前进和撤退重试，以及比赛结束紧接发送目标的情况。记录每个 goal ID 的发送、拒绝/接受、取消和终态时间。
- 判据：拒绝不得被当作成功，失败后按 2 秒退避重试；新目标不与未终止的旧目标重叠，比赛结束后没有仍在执行的旧目标。不能以树节点 `onHalted()` 或 Nav2 可能抢占旧目标代替服务端证据。
- 结果：第六轮复核仍未运行；本机无 ROS/假 Nav2 环境。若失败，优先补充 BT-001/BT-006，并复核 BT-006 当前的低风险评级。

### 待测-02：比赛结束时取消等待推迟停车

- 风险：`高`；关联：BT-013、BT-001；状态：`待测试`。
- 测试：目标运行中让比赛结束，假 Nav2 延迟取消响应和最终结果；同时记录比赛状态、根部四项零输出、`/cmd_vel_nav2_result` 和 Hub 底盘输出的时间。另在比赛仍进行时触发单航点 60 秒超时，量出取消等待期间的控制心跳间隔。
- 判据：量出结束至底盘停止的延迟；结束后的取消等待期间不应继续输出非零运动命令。比赛仍进行时，云台、spin、自瞄的连续心跳间隔应小于 0.5 秒。
- 结果：未运行；不能仅凭 XML 的最终零值认定及时停车。若失败，补充 BT-013 的安全收口验收。

### 待测-03：零 Twist 与 spin 归零的跨话题顺序

- 风险：`中`；关联：BT-013；状态：`待测试`。
- 测试：RC 输入保持中立，先令 COMBAT 或 RECOVER 发布 `spin=7`；比赛结束时分别让 `spin=0` 与零 Twist 先后到达 `fake_vel_transform`，观察其 `/cmd_vel` 和 Hub 底盘转速。
- 判据：任一到达顺序下，比赛结束后不得因零 Twist 叠加旧 spin 而继续输出非零底盘转速。
- 结果：未运行；若复现，补充 BT-013，并定位速度转换或 Hub 仲裁的责任层。

### 待测-04：ROS 模拟时间与恢复墙钟的差异

- 风险：`中（仿真）`；关联：BT-016；状态：`待测试`。
- 测试：`use_sim_time=true`、HP=350 时分别暂停、加速和回拨模拟时钟，记录 `WaitForRecovery` 的 30 秒出发时刻与 Nav2 goal。与 BT-016 已记录的 Python 墙钟语义对照。
- 判据：确认实际计时源，并明确仿真是否接受按系统时间恢复；若要求跟随模拟时间，再单独提出语义变更，不把等价迁移记成新缺陷。
- 结果：未运行；BT-016 已记录计时源决定，此处只归档待测证据。

### 待测-05：比赛中取消 ExecuteTree 或 Control+C 后输出残留

- 风险：`高`；关联：BT-010、BT-013；状态：`待测试`。
- 测试：COMBAT 已发布自瞄 1、`spin=7.0` 时，分别取消 ExecuteTree 和发送 Control+C；从独立 ROS 观察者记录 action 终态、四项控制 topic 及 Hub 底盘输出。
- 判据：两种退出均完成安全停止；不能以 `haltTree()`、黑板 `aim_allowed=0` 或 action 已结束代替四项实际零输出。
- 结果：未运行；静态确认两种路径不进入根部安全分支，Control+C 路径也未显式 `haltTree()`。

### 待测-06：COMBAT 入口输出后阈值异常中止执行

- 风险：`高`；关联：BT-008、BT-013；状态：`待测试`。
- 测试：隔离测试树在 COMBAT 入口发布完成后，令 `TrackHeat` 使用非法阈值；记录 ExecuteTree 终态和四项控制 topic。
- 判据：确认 abort 和错误信息，并证明异常后实际输出能安全停止；不能将根部 `Fallback` 的正常 `FAILURE` 行为算作本用例通过。
- 结果：未运行；正式 XML 阈值合法，需故障注入。

### 待测-07：根部安全发布返回 FAILURE

- 风险：`高`；关联：BT-013、BT-019；状态：`待测试`。
- 测试：正常比赛结束后，分别注入根部四个发布节点返回 `FAILURE`，记录每项零指令尝试、实际输出和 ExecuteTree 终态。
- 判据：任一安全发布未完成时不得报告安全停止成功；其他可发送的零指令仍应尝试发送，失败原因可见。
- 结果：未运行；静态确认现有 `Sequence` 会跳过后续发布、`ForceSuccess` 仍返回 `SUCCESS`。

### 待测-08：比赛中 GameStatus 消息断流

- 风险：`中`；关联：BT-013；状态：`待测试`。
- 测试：先发布 `game_progress=4`，随后停止 `/referee/common/game_status`，观察各阶段 gate、导航目标、控制输出与实际比赛状态。
- 判据：记录断流后继续运行的时间和输出；先确定是否要求裁判消息超时即安全停止，再据此决定是否修改行为。原 Python 也缓存最后状态，现阶段不把该等价行为写成新增迁移缺陷。
- 结果：未运行；静态确认订阅黑板只保存最后一条消息，`IsGameStatus` 不检查新鲜度。

### 待测-09：IsGameStatus 整数端口读取失败

- 风险：`中`；关联：BT-023；状态：`待测试`。
- 测试：隔离测试树把 `expected_game_progress`、`min_remain_time`、`max_remain_time` 分别绑定到缺失或类型错误的黑板值，在比赛中 tick 并记录 gate、目标和输出。
- 判据：每种错误均得到确定的安全结果；不得把比赛门误判为通过，也不得使用未初始化值继续发送目标或非零控制。
- 结果：未运行；正式 XML 使用有效字面值，代码层未检查这三个 `getInput` 的返回结果。

### 待测-10：当前 HEAD 的 ROS 构建与五个 competition CTest

- 风险：`高`；关联：BT-007、BT-011、BT-013、BT-015、BT-016；状态：`待测试`。
- 测试：在具备 ROS 2 和完整依赖的隔离 Linux 工作空间，记录代码 HEAD、`ROS_DISTRO` 与 `behaviortree_cpp` 版本；运行 `colcon build --packages-select pb2025_sentry_behavior --cmake-args -DBUILD_TESTING=ON`、`colcon test --packages-select pb2025_sentry_behavior --ctest-args -R '^competition_(conditions|combat|retreat|recover|phase1)$' --output-on-failure`、`colcon test-result --verbose`，保存退出码和关键日志。
- 判据：本 HEAD 构建成功，五个 CTest 全部通过；CTest 中的记录型发布/导航桩不能充当真实 ROS 接口证据。条件调试 XML 的独立覆盖缺口仍由 BT-014 跟踪。
- 结果：未运行，无构建或 CTest 退出码。2026-09-27 在 macOS 上，`colcon --version`、`ros2 --version`、`cmake --version` 均退出 127，提示 `command not found`；ROS/BT.CPP 运行版本不可取得。HEAD 为 `50ad0e4757a3d475799451521b03ab6f474e181d`。四份正式 XML 可解析，五个 CTest 已注册，但这仅是静态检查；旧 `sentry_ws/install/setup.bash` 指向本机不存在的 `/opt/ros/humble`，不能复用为本 HEAD 的验证结果。

### 待测-11：真实 PoseStamped 目标的转换与 Nav2 接收

- 风险：`中`；关联：BT-015；状态：`待测试`。
- 测试：在独立 `ROS_DOMAIN_ID` 使用正式 `competition_phase1` 和真实 `SendNav2Goal` 插件，配合假 Nav2 action server 记录收到的 `PoseStamped`；核对正式前进/撤退目标的 x、y、`frame_id=map`、时间戳和零 yaw 四元数。另用隔离测试输入让同一真实插件转换一次非零 yaw。
- 判据：正式目标数值和坐标系正确，真实 Nav2 action 接收成功；非零 yaw 另行证明转换代码。字符串目标桩或单独调试树的通过结果不能替代正式树接口验证。
- 结果：未运行；当前机器没有 ROS 2 或可用假 Nav2 服务端，现有 CTest 只记录目标字符串。

### 待测-12：正式树控制话题类型、重映射与心跳

- 风险：`高`；关联：BT-011、BT-013、BT-015、BT-016；状态：`待测试`。
- 测试：在独立 `ROS_DOMAIN_ID` 启动正式 `competition_phase1`，使用模拟裁判输入、假 Nav2 和实际 ROS 发布插件；用 `ros2 topic type`、`echo`、`hz` 记录 `/cmd_vel_nav2_result`、`/cmd_spin`、`/gimbal_scan_cmd`、`/auto_aim_switch` 的类型、值、重映射及阶段运行/结束时序。
- 判据：四种类型符合 `test/README.md`；XML 的 `cmd_vel` 实际到达 `/cmd_vel_nav2_result`；云台、自瞄、spin 稳定心跳间隔小于 0.5 秒，比赛结束可观察四项零输出。独立调试树结果不替代正式树结果。
- 结果：未运行；`ros2 --version` 退出 127，没有 ROS topic 观测数据。不得在实车控制域发布模拟裁判或控制消息。

---

## BT-023：IsGameStatus 未检查整数端口读取结果

- 状态：`待处理`
- 优先级：`中`
- 发现日期：2026-09-27
- 涉及文件：`plugins/condition/is_game_status.cpp`

### 问题

`checkGameStart()` 声明三个未初始化的整数，调用 `getInput()` 读取 `expected_game_progress`、`min_remain_time`、`max_remain_time` 后不检查结果，随即用这些值比较比赛状态（`plugins/condition/is_game_status.cpp:28-48`）。若动态黑板绑定缺失或端口读取失败，比较结果不可靠，可能错误放行比赛 gate 或错误退出。当前正式 XML 提供有效字面值，尚未发现现有配置触发此问题。

### 建议处理

明确端口读取失败时的安全语义，检查三个返回结果后再比较；运行验证场景归 BT-022 待测-09。

### 验收条件

- [ ] 三个整数端口分别读取失败时，条件节点有确定结果，不使用未初始化值。
- [ ] 端口错误不能使比赛 gate 放行目标或非零控制；当前正式 XML 的正常比赛路径保持不变。

---

## 新问题模板

复制本节并分配下一个编号。

### BT-XXX：问题标题

- 状态：`待处理`
- 优先级：`高 / 中 / 低`
- 发现日期：YYYY-MM-DD
- 涉及文件：
- 参考实现：

#### 问题

描述当前行为、触发条件和预期行为。

#### 影响

- 待补充。

#### 建议处理

记录建议方向；方案未确定时不要提前写死实现。

#### 验收条件

- [ ] 待补充。

#### 解决记录

- 修复提交：
- 验证环境：
- 验证结果：
- 备注：
