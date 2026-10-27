# Odin 原生三维地图的比赛流程

当前 `competition_phase1` 使用 `field_v2_20261024` 原生定位图和配套导航图，以及 2026-10-27 选取的六个点。前进依次经过第 1 至第 6 点，撤退按逆序返回，各点保留选点时的朝向。开赛、战斗、热量、血量、恢复和结束处理继续使用原比赛行为树。

- 原生定位图：`~/sentry_ws/maps/field_v2_20261024/native.bin`。
- 导航图：`~/sentry_ws/maps/field_v2_20261024/navigation_ground_continuous/navigation.yaml`。
- 六点记录：`~/sentry_ws/maps/field_v2_20261024/waypoints.yaml`。
- 建图与选点操作：[odin_3d_test/USAGE.md](../odin_integration/odin_3d_test/USAGE.md)。

云台在初始化和开赛前以 `0.5 rad/s` 扫描，底盘保持零速。前进、撤退阶段由 Hub 按导航速度方向控制云台，停车后保持朝向。前进、撤退的小陀螺指令为 `4.0 rad/s`，战斗及恢复为 `7.0 rad/s`；遥控接管与急停沿用 Hub 的现有处理。

1. 终端1跑导航：

```bash
source /opt/ros/humble/setup.bash
source "$HOME/sentry_ws/install/setup.bash"
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0 OPENBLAS_NUM_THREADS=1 TZ=Asia/Shanghai
ODIN_MAP_DIR="$HOME/sentry_ws/maps/field_v2_20261024"

env -u PYTHONNOUSERSITE ros2 launch odin_3d_test odin_3d.launch.py \
  mode:=navigation \
  native_map:="$ODIN_MAP_DIR/native.bin" \
  map:="$ODIN_MAP_DIR/navigation_ground_continuous/navigation.yaml"
```

这条命令同时启动原生全局定位、点云处理与 Nav2。默认导航上限为 `4.0 m/s`，控制器和平滑器共用该上限；平移加速度和减速度均为 `2.5 m/s²`，弯道横向加速度上限为 `2.5 m/s²`。控制器继续根据剩余路径、前方曲率与障碍距离减速，末段速度为 `0.15 m/s`，进入 10 cm 目标范围后停车。4 m/s 对应的提前减速距离为约 4.5 m。 目标接近速度按剩余路径计算可停车速度上限；短目标仍可正常加速，4.5 m 用作全速停车预算。

中间航点进入 0.35 m 范围后从待经过列表移除。前视时间为 0.6 s，最大前视距离为 1 m；平移比例增益为 4.0、微分增益为 0.0，目标点和障碍物的距离制动继续生效。

raw 处理范围为 10 m，近点密度过滤范围保持 4 m；地形窗口为 18×18 m，分辨率 5 cm，更新设置为 10 Hz。局部代价地图至少 10×10 m，前方检查范围约 4.9 m。

摇杆最大输出只作用于手动输入。导航分支直接使用 `/cmd_vel`；底盘继续受 1200 rpm 轮速上限和功率分配影响。高平移与高速旋转同时请求时，底盘可能按轮速比例缩放实际输出。`4 m/s` 与行进时的 `4 rad/s` 表示指令上限及目标值，实车表现按轮速和运动记录核对。

启动时保持车辆静止。行为树等待地图、TF、Nav2 和裁判状态期间持续扫描云台。原生定位状态见 `/odin_3d/state`、`/odin_3d/ready`；开赛前核对 `READY` 和 RViz 中车辆位置、朝向。

2. 终端2跑行为树：

```bash
cd "$HOME/sentry_ws"
source /opt/ros/humble/setup.bash
source install/setup.bash
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0 TZ=Asia/Shanghai

mkdir -p log/behavior_spin_detail
SENTRY_A="$PWD/log/behavior_spin_detail/A_$(date +%Y%m%d_%H%M%S)"

script -q -f -e \
  --log-out "$SENTRY_A.log" \
  --log-timing "$SENTRY_A.time" \
  --command 'ros2 launch behavior pb2025_sentry_behavior_launch_new.py \
    params_file:="$HOME/sentry_ws/src/behavior/params/sentry_behavior_phase1.yaml"'
```

使用真实裁判系统时，沿用现有 `referee_serial_launch.py` 服务，
由 `/referee/common/game_status`、`robot_performance`、`robot_heat` 驱动同一棵树。
以下第 3 步是原有模拟裁判输入，供单独模拟测试使用。

3. 终端3模拟比赛开始及完整裁判流程：

程序位于本包的 `scripts/` 目录：

- [run_competition_trial_recorded.sh](scripts/run_competition_trial_recorded.sh)：推荐入口，保存终端输出、运行参数与 rosbag，随后调用裁判模拟程序。
- [run_competition_trial.sh](scripts/run_competition_trial.sh)：发布模拟裁判消息，并按行为树反馈推进比赛阶段。

```bash
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0
bash "$HOME/sentry_ws/src/behavior/scripts/run_competition_trial_recorded.sh"
```

脚本通过新导航请求确认前进与撤退，通过恢复阶段的 0.5 rad/s 云台扫描确认到达；行进旋转为 4 rad/s，战斗和恢复旋转为 7 rad/s。

脚本会加载 ROS 和 `sentry_ws` 环境。运行前完成终端 1 的定位核对，并启动终端 2 的
`competition_phase1`。本轮模拟测试的裁判话题使用模拟程序这一组发布者。

开赛过程如下：

1. 持续发布准备阶段 `game_progress=3`、HP `400`、热量 `0`。
2. 等待 `/bt_navigator` 进入 `active` 状态，再读取行为树的 `/cmd_spin=0.0` 待机输出。
3. 自动切换为 `game_progress=4`，以 `10 Hz` 持续发布开赛状态。
4. 按行为树反馈依次模拟热量变化、低血量撤退、恢复、再次前进和比赛结束。

开赛消息会触发现有比赛树实际执行车辆动作。若终端停在
`等待正式树完成初始化并进入开赛前状态`，请查看终端 2 的行为树输出；
该阶段正在等待 `/cmd_spin=0.0`；此前还会等待 Nav2 激活。当前路线为六个巡航点。
记录保存在 `~/sentry_ws/log/behavior_spin_detail/`。

4. 最后一次成功运行的日志文件：

按最新一轮 **2026-10-03 14:56—14:57** 的记录，日志位于：

- **测试主目录：[behavior_spin_detail](/home/soyo/sentry_ws/log/behavior_spin_detail)**包含导航 `N_20261003_145604.log`、行为树 `A_20261003_145632.log`、测试终端 `C_20261003_145642.log`，以及裁判输入、参数、话题和发布者记录。
- **本轮消息记录：[bag_20261003_145642](/home/soyo/sentry_ws/log/behavior_spin_detail/bag_20261003_145642)**包含 `bag_20261003_145642_0.db3` 和 `metadata.yaml`，用于检查导航、底盘指令和电机反馈。
- **导航节点日志：[nav_runtime/20261003_145604](/home/soyo/sentry_ws/log/nav_runtime/20261003_145604)**包含 Nav2、定位、雷达、地形处理、RViz 等节点日志，以及 `localization.json`。
- **ROS 原生日志：[~/.ros/log](/home/soyo/.ros/log)**包含 LK、控制器、行为树节点日志及各次启动目录。本轮相关启动目录为：

  - [控制器：14:51:35](/home/soyo/.ros/log/2026-10-03-14-51-35-191379-soyo-mygo-3653)
  - [导航：14:56:04](/home/soyo/.ros/log/2026-10-03-14-56-04-886233-soyo-mygo-23516)
  - [行为树：14:56:33](/home/soyo/.ros/log/2026-10-03-14-56-33-158338-soyo-mygo-24066)
- **此次重新编译日志：[build_2026-10-03_14-22-46](/home/soyo/sentry_ws/log/build_2026-10-03_14-22-46)**
  `chassis_controllers/` 子目录保存该包的编译输出。

修复前分析使用的记录也保存在第 1 项目录，核心消息目录是 [bag_20261003_014822](/home/soyo/sentry_ws/log/behavior_spin_detail/bag_20261003_014822)。后续对照可使用 **`014822`（修复前）与 `145642`（最新一轮）**。
