车

测试指令：

当前 `competition_phase1` 使用 Odin 地图 `odin_20261019_220142` 和 2026-10-20 选择的六个点。
前进依次经过第 1 至第 6 点，撤退按逆序返回。各点保留选点时的朝向。
开赛、战斗、热量、血量、恢复和结束处理沿用原比赛行为树。

地图文件位于 `pb2025_nav_bringup/map/reality/odin_20261019_220142.yaml`，
对应图像为同目录下的 `odin_20261019_220142.pgm`。
原始选点记录为 `~/odin_ws/routes/odin_competition_20261020_201546.yaml`。

云台在初始化阶段以 `0.5 rad/s` 持续扫描，帮助定位获取不同方向的观测。
等待地图、定位、Nav2 以及开赛期间，行为树每次 tick 都给出底盘零速、
小陀螺零速和自瞄关闭指令。`PublishTwist` 经启动文件转到 `/cmd_vel_nav2_result`，
再由 `fake_vel_transform` 发布 `/cmd_vel`，持续刷新 Hub 的导航输入。
单次零速在 Hub 的 `0.2 s` 有效期后会恢复遥控输入，导航档附带的自转也会随之恢复。
前进、撤退阶段由 Hub 按导航速度方向控制云台，停车后保持朝向。
上一阶段的扫描指令在最多 `0.5 s` 内到期后，Hub 切换到运动方向跟随。
开赛前待机、战斗与恢复阶段继续使用各自的扫描设置，遥控接管与急停沿用 Hub 的现有处理。

`universal_controller/config/controller_params.sentry.yaml` 中，跟随目标的最大角速度
从 `1.0` 调至 `3.0 rad/s`，最大角加速度从 `2.0` 调至 `6.0 rad/s²`。
普通导航与比赛行进共用这组参数，实际转向速度与 Odin 数据连续性需实车确认。
首次加载本次调整时，结束已有导航和行为树，车辆保持急停，在终端执行：

```bash
sudo systemctl restart ros2-universal-controller.service
```

控制服务恢复后，按原遥控操作进入允许导航的状态、摇杆回中，再依次启动下面的终端 1、2。
两个终端共同完成初始化；导航启动后及时启动行为树，让云台扫描参与重定位。
行为树客户端在发现服务端后留出 `2 s` 供回复通道建立，再发送一次执行请求。
终端出现 `Behavior Tree startup request accepted: competition_phase1` 表示服务端已接收请求。
如果持续显示 `Waiting for Behavior Tree goal response`，保持车辆急停，检查本轮启动日志。
恢复导航档前先确认 `/gimbal_scan_cmd` 与底盘零速正在持续发布。

1. 终端1跑导航：

```bash
cd "$HOME/sentry_ws"
source /opt/ros/humble/setup.bash
source install/setup.bash
source "$HOME/odin_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0 TZ=Asia/Shanghai

mkdir -p log/behavior_spin_detail
SENTRY_N="$PWD/log/behavior_spin_detail/N_$(date +%Y%m%d_%H%M%S)"

script -q -f -e \
  --log-out "$SENTRY_N.log" \
  --log-timing "$SENTRY_N.time" \
  --command 'ros2 launch odin_sentry_bringup odin_sentry.launch.py \
    mode:=navigation \
    use_robot_state_pub:=True \
    use_rviz:=True \
    map:="$(ros2 pkg prefix --share pb2025_nav_bringup)/map/reality/odin_20261019_220142.yaml" \
    params_file:="$HOME/sentry_ws/log/behavior_speed_test/nav2_3p0_no_spin.yaml" \
    linear_speed_limit:=3.0 \
    angular_speed_limit:=3.0'
```

此入口启动已完成的 Odin 双路感知与 AMCL 全局初始化，并沿用 2026-10-03
完整比赛流程测试的 `nav2_3p0_no_spin.yaml`。控制器线速度上限为 `2.5 m/s`，
速度平滑器的 X/Y 上限为 `3.0 m/s`，Nav2 角速度范围为 `[-3.0, 3.0] rad/s`。
启动参数的两个 `3.0` 保留上述原始设置；控制器最终仍使用配置文件中的 `2.5 m/s`。
Odin 入口的到点条件为位置误差 `0.1 m` 内且平移速度不高于 `0.1 m/s`，
当前比赛配置的近终点减速距离为 `2.4 m`。末段停车与修正独立于最高速度设置。
行为树经 `/cmd_spin` 给出的行进 `2.2 rad/s`、战斗及恢复 `7.0 rad/s` 沿用原值；
这部分旋转由后级合成，需按原比赛操作流程管理。
启动后等待地图、当前扫描、局部 TF 和 AMCL 工作状态就绪，
随后用已收到的近期扫描自动搜索整张地图的位置与朝向，经过三帧新扫描复核后，
向 AMCL 提供初始位姿，并在剩余总时间内继续观察。Odin 默认参数为 `localization_method:=scan_match`。
这段观察与 Nav2 激活并行进行，比赛树按原有地图、TF、导航服务与裁判状态推进。
全局搜索与候选复核都合并最近 `10 s` 的二维扫描，按各帧测量时刻的车体位姿对齐。
候选复核还检查最新单帧的匹配质量，连续三帧通过后才提交初始位姿。
AMCL 继续使用当前扫描。
初始化期间的自动云台扫描持续提供新视角，raw 实时障碍按 `0.2 s` 保留。
导航开始输出日志后即可启动终端 2。输入等待、搜索和三帧复核共用 `60 s` 总预算，
每轮候选复核最多 `20 s`，同时受剩余总时间限制。到时仍未通过则退出并报告超时。
`--startup-timeout`、`--search-timeout` 和 `--verification-timeout` 可以缩短各阶段等待时间，
重定位总预算上限保持 `60 s`，后续定位观察也使用剩余时间。
若终端显示初始化已超时，重新启动导航和行为树这一组入口。

对应历史记录为 `log/behavior_spin_detail/params_20261003_145642.log` 和
`C_20261003_145642.log`：使用实车与模拟裁判输入，完成前进、战斗、撤退、
恢复、再次前进和结束。现场速度还受路径、障碍物、加减速及底盘功率限制影响。

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
