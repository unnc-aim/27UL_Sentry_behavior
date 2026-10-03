#!/usr/bin/env bash
# 用法：导航和正式行为树启动后，第三个终端直接运行本脚本。
set -eo pipefail

SENTRY_WS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
export SENTRY_LOG_DIR="${SENTRY_LOG_DIR:-$SENTRY_WS/log/behavior_spin_detail}"
export TZ=Asia/Shanghai
export SENTRY_RECORD_STAMP="${SENTRY_RECORD_STAMP:-$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$SENTRY_LOG_DIR"

# script 自动保存终端输出和时间记录；内部参数用于进入实际执行部分。
if [[ "${1:-}" != "--recording" ]]; then
  printf -v SENTRY_COMMAND 'bash %q --recording' \
    "$SENTRY_WS/src/behavior/scripts/run_competition_trial_recorded.sh"
  exec script -q -f -e \
    --log-out "$SENTRY_LOG_DIR/C_$SENTRY_RECORD_STAMP.log" \
    --log-timing "$SENTRY_LOG_DIR/C_$SENTRY_RECORD_STAMP.time" \
    --command "$SENTRY_COMMAND"
fi

source /opt/ros/humble/setup.bash
source "$SENTRY_WS/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}"
export ROS_LOCALHOST_ONLY="${ROS_LOCALHOST_ONLY:-0}"
export PYTHONUNBUFFERED=1
SENTRY_BAG_DIR="$SENTRY_LOG_DIR/bag_$SENTRY_RECORD_STAMP"

log()
{
  printf '[%s] %s\n' "$(date '+%F %T %z')" "$*"
}

capture()
{
  local output="$1"
  shift  # 后续参数组成需要执行的查询命令。
  printf '\n[%s] %s\n' "$(date '+%F %T %z')" "$*" >>"$output"
  # 参数和发布者查询用于补充记录；失败时保存原始错误并继续测试。
  if ! "$@" >>"$output" 2>&1; then
    log "记录查询失败：$*；详情：$output；继续测试。"
  fi
}

cleanup()
{
  local result=$?
  trap - EXIT INT TERM
  # SIGINT 让 rosbag 写完剩余数据；wait 等待文件保存完成。
  kill -INT "$SENTRY_BAG_PID" 2>/dev/null || true
  wait "$SENTRY_BAG_PID" 2>/dev/null || true
  ros2 bag info "$SENTRY_BAG_DIR" || true
  log "记录结束，退出码：$result；bag：$SENTRY_BAG_DIR"
  exit "$result"
}

log "开始记录导航、底盘和电机反馈：$SENTRY_BAG_DIR"
ros2 bag record --include-hidden-topics -o "$SENTRY_BAG_DIR" \
  /cmd_vel_controller /cmd_vel_nav2_result /cmd_vel /cmd_spin \
  /chassis_command /gimbal_scan_cmd /auto_aim_switch \
  /universal_controller/input/ndj /universal_controller/input/vtm \
  /ecat/sn2228292/app1/read /ecat/sn2228292/app1/write \
  /ecat/sn2228292/app2/read /ecat/sn2228292/app2/write \
  /ecat/sn4128829/app1/read /ecat/sn4653115/app2/read \
  /joint_states /tf /tf_static \
  /odometry /lidar_odometry /aft_mapped_to_init /amcl_pose \
  /plan /local_plan /lookahead_point /curvature_points_marker_array \
  /map /local_costmap/costmap_raw /global_costmap/costmap_raw \
  /terrain_map /terrain_map_ext /obstacle_scan \
  /navigate_through_poses/_action/feedback /navigate_through_poses/_action/status \
  /follow_path/_action/feedback /follow_path/_action/status \
  /referee/common/game_status /referee/common/robot_performance \
  /referee/common/robot_heat /parameter_events /rosout \
  >"${SENTRY_BAG_DIR}.log" 2>&1 </dev/null &
SENTRY_BAG_PID=$!
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

log "保存运行参数和发布者信息。"
# --no-daemon 直接发现当前节点，查询使用本终端的 ROS 环境。
for SENTRY_NODE in /controller_server /velocity_smoother /fake_vel_transform \
  /universal_controller_hub /lk_full_wheel_chassis_controller; do
  capture "$SENTRY_LOG_DIR/params_$SENTRY_RECORD_STAMP.log" \
    ros2 param dump --no-daemon "$SENTRY_NODE"
done
capture "$SENTRY_LOG_DIR/topics_$SENTRY_RECORD_STAMP.log" \
  ros2 topic list --no-daemon -t
for SENTRY_TOPIC in /cmd_vel /chassis_command \
  /ecat/sn2228292/app1/write /ecat/sn2228292/app2/write; do
  capture "$SENTRY_LOG_DIR/publishers_$SENTRY_RECORD_STAMP.log" \
    ros2 topic info --no-daemon -v "$SENTRY_TOPIC"
done

if ! kill -0 "$SENTRY_BAG_PID" 2>/dev/null; then
  log "bag 录制进程已退出，请检查 ${SENTRY_BAG_DIR}.log。"
  exit 1
fi
log "记录查询完成，启动完整比赛测试。"
bash "$SENTRY_WS/src/behavior/scripts/run_competition_trial.sh"
