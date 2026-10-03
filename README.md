测试指令：


1. 终端1跑导航：

```bash
cd /home/soyo/sentry_ws
source /opt/ros/humble/setup.bash
source install/setup.bash
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0 TZ=Asia/Shanghai

mkdir -p log/behavior_spin_detail
SENTRY_N="$PWD/log/behavior_spin_detail/N_$(date +%Y%m%d_%H%M%S)"

script -q -f -e \
  --log-out "$SENTRY_N.log" \
  --log-timing "$SENTRY_N.time" \
  --command 'ros2 launch pb2025_nav_bringup rm_navigation_reality_launch.py \
    slam:=False \
    world:=field_20260925 \
    use_robot_state_pub:=True \
    use_rviz:=True \
    auto_global_localization:=True \
    log_level:=controller_server:=debug \
    map:=/home/soyo/sentry_ws/src/pb2025_sentry_nav/pb2025_nav_bringup/map/reality/field_20260925.yaml \
    params_file:=/home/soyo/sentry_ws/log/behavior_speed_test/nav2_3p0_no_spin.yaml'
```

2. 终端2跑行为树：

```bash
cd /home/soyo/sentry_ws
source /opt/ros/humble/setup.bash
source install/setup.bash
export ROS_DOMAIN_ID=0 ROS_LOCALHOST_ONLY=0 TZ=Asia/Shanghai

mkdir -p log/behavior_spin_detail
SENTRY_A="$PWD/log/behavior_spin_detail/A_$(date +%Y%m%d_%H%M%S)"

script -q -f -e \
  --log-out "$SENTRY_A.log" \
  --log-timing "$SENTRY_A.time" \
  --command 'ros2 launch behavior pb2025_sentry_behavior_launch_new.py \
    params_file:=/home/soyo/sentry_ws/src/behavior/params/sentry_behavior_phase1.yaml'
```

3. 终端3跑模拟裁判系统测试指令集：

```bash
cd /home/soyo/sentry_ws
bash /home/soyo/sentry_ws/src/behavior/scripts/run_competition_trial_recorded.sh
```

4. 最后一次成功运行的日志文件：

按最新一轮 **2026-10-03 14:56—14:57** 的记录，日志位于：

- **测试主目录：[behavior_spin_detail](/home/soyo/sentry_ws/log/behavior_spin_detail)**  
   包含导航 `N_20261003_145604.log`、行为树 `A_20261003_145632.log`、测试终端 `C_20261003_145642.log`，以及裁判输入、参数、话题和发布者记录。

- **本轮消息记录：[bag_20261003_145642](/home/soyo/sentry_ws/log/behavior_spin_detail/bag_20261003_145642)**  
   包含 `bag_20261003_145642_0.db3` 和 `metadata.yaml`，用于检查导航、底盘指令和电机反馈。

- **导航节点日志：[nav_runtime/20261003_145604](/home/soyo/sentry_ws/log/nav_runtime/20261003_145604)**  
   包含 Nav2、定位、雷达、地形处理、RViz 等节点日志，以及 `localization.json`。

- **ROS 原生日志：[~/.ros/log](/home/soyo/.ros/log)**  
   包含 LK、控制器、行为树节点日志及各次启动目录。本轮相关启动目录为：
   - [控制器：14:51:35](/home/soyo/.ros/log/2026-10-03-14-51-35-191379-soyo-mygo-3653)
   - [导航：14:56:04](/home/soyo/.ros/log/2026-10-03-14-56-04-886233-soyo-mygo-23516)
   - [行为树：14:56:33](/home/soyo/.ros/log/2026-10-03-14-56-33-158338-soyo-mygo-24066)

- **此次重新编译日志：[build_2026-10-03_14-22-46](/home/soyo/sentry_ws/log/build_2026-10-03_14-22-46)**  
   `chassis_controllers/` 子目录保存该包的编译输出。

修复前分析使用的记录也保存在第 1 项目录，核心消息目录是 [bag_20261003_014822](/home/soyo/sentry_ws/log/behavior_spin_detail/bag_20261003_014822)。后续对照可使用 **`014822`（修复前）与 `145642`（最新一轮）**。


