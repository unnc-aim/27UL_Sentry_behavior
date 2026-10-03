#!/usr/bin/env bash
# 用法：导航和正式行为树启动后，在第三个终端运行本脚本。
# 对应 competition_phase1：通过实际输出推进阶段，所有控制命令仍由行为树发布。
set -eo pipefail

SENTRY_WS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
source /opt/ros/humble/setup.bash
source "$SENTRY_WS/install/setup.bash"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}"
export ROS_LOCALHOST_ONLY="${ROS_LOCALHOST_ONLY:-0}"
export PYTHONUNBUFFERED=1
export TZ=Asia/Shanghai

SENTRY_LOG_DIR="${SENTRY_LOG_DIR:-$SENTRY_WS/log/behavior_auto}"
mkdir -p "$SENTRY_LOG_DIR"
SENTRY_STAMP="$(date +%Y%m%d_%H%M%S)"
SENTRY_LOG="$SENTRY_LOG_DIR/referee_${SENTRY_STAMP}.log"
declare -A SENTRY_PUBLISHERS=()  # 每个裁判话题对应一个持续发布进程。
SENTRY_COMPLETE=0
SENTRY_LAST_MESSAGE=""

log()
{
  printf '[%s] %s\n' "$(date '+%F %T %z')" "$*" | tee -a "$SENTRY_LOG"
}

cleanup()
{
  local result=$?
  trap - EXIT INT TERM  # 清理期间使用一次退出处理。
  local pid
  for pid in "${SENTRY_PUBLISHERS[@]}"; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  if [[ "$SENTRY_COMPLETE" == 0 && -n "${SENTRY_PUBLISHERS[game_status]:-}" ]]; then
    log "测试中断，补发比赛结束状态；现场停车使用遥控急停。"
    ros2 topic pub --once --wait-matching-subscriptions 0 \
      /referee/common/game_status dji_referee_protocol/msg/GameStatus \
      '{game_type: 1, game_progress: 5, stage_remain_time: 0}' \
      >>"$SENTRY_LOG" 2>&1 || true
  fi
  log "记录结束，退出码：$result"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

publish_referee()
{
  local name="$1" type="$2" data="$3"
  local old_pid="${SENTRY_PUBLISHERS[$name]:-}"
  if [[ -n "$old_pid" ]]; then
    kill "$old_pid" 2>/dev/null || true
    wait "$old_pid" 2>/dev/null || true
  fi
  log "输入 $name：$data"
  # 10Hz 持续发送当前值；切换状态时仅替换该话题的发布进程。
  ros2 topic pub --rate 10 --print 0 --wait-matching-subscriptions 0 \
    --node-name "trial_${name}" \
    "/referee/common/$name" "dji_referee_protocol/msg/$type" "$data" \
    >>"$SENTRY_LOG_DIR/input_${name}_${SENTRY_STAMP}.log" 2>&1 &
  SENTRY_PUBLISHERS[$name]=$!
}

wait_for()
{
  local label="$1" topic="$2" type="$3" expression="$4"
  shift 4
  log "$label"
  # --once 在第一条满足条件的消息到达后返回；m 表示整条 ROS 消息。
  SENTRY_LAST_MESSAGE="$(ros2 topic echo "$topic" "$type" \
    --once --filter "$expression" "$@")"
  log "$SENTRY_LAST_MESSAGE"
}

log "流程：开赛、前进、热量停用与恢复、撤退、回满血、再次前进、结束。"
log "记录文件：$SENTRY_LOG"
publish_referee game_status GameStatus '{game_type: 1, game_progress: 3, stage_remain_time: 420}'
publish_referee robot_performance RobotPerformance '{robot_id: 7, current_hp: 400, maximum_hp: 400}'
publish_referee robot_heat RobotHeat '{shooter_17mm_barrel_heat: 0}'
wait_for "等待正式树完成初始化并进入开赛前状态" /cmd_spin example_interfaces/msg/Float32 'm.data == 0.0'

publish_referee game_status GameStatus '{game_type: 1, game_progress: 4, stage_remain_time: 420}'
wait_for "开赛；等待五点前进完成并进入战斗" /gimbal_scan_cmd pb_rm_interfaces/msg/GimbalCmd 'm.velocity.yaw == 1.0'
wait_for "确认战斗自瞄开启" /auto_aim_switch std_msgs/msg/Int32 'm.data == 1'
publish_referee robot_heat RobotHeat '{shooter_17mm_barrel_heat: 240}'
wait_for "热量240；确认自瞄关闭" /auto_aim_switch std_msgs/msg/Int32 'm.data == 0'
publish_referee robot_heat RobotHeat '{shooter_17mm_barrel_heat: 50}'
wait_for "热量50；确认自瞄恢复" /auto_aim_switch std_msgs/msg/Int32 'm.data == 1'

publish_referee robot_performance RobotPerformance '{robot_id: 7, current_hp: 150, maximum_hp: 400}'
# Float32 的2.2有表示误差，使用差值比较；退回到点后才会重新发布7。
wait_for "HP150；确认进入撤退" /cmd_spin example_interfaces/msg/Float32 'abs(m.data - 2.2) < 0.05'
wait_for "等待逆序五点撤退完成并进入恢复" /cmd_spin example_interfaces/msg/Float32 'm.data == 7.0'
publish_referee robot_performance RobotPerformance '{robot_id: 7, current_hp: 400, maximum_hp: 400}'
wait_for "回满血；确认开始下一轮前进" /cmd_spin example_interfaces/msg/Float32 'abs(m.data - 2.2) < 0.05'
wait_for "等待第二次前进完成并进入战斗" /gimbal_scan_cmd pb_rm_interfaces/msg/GimbalCmd 'm.velocity.yaw == 1.0'

publish_referee game_status GameStatus '{game_type: 1, game_progress: 5, stage_remain_time: 0}'
# Action状态采用持久QoS，晚订阅也能读到已完成的结果状态。
wait_for "比赛结束；读取行为树最终状态" /pb2025_sentry_behavior/_action/status \
  action_msgs/msg/GoalStatusArray 'm.status_list and m.status_list[-1].status in (4, 5, 6)' \
  --qos-durability transient_local --qos-reliability reliable
# awk 保留最后一个 status，与上方 status_list[-1] 对应；4 表示成功。
if ! awk '/^[[:space:]]*status:/ {code=$2} END {exit(code != 4)}' <<<"$SENTRY_LAST_MESSAGE"; then
  log "行为树以取消或失败状态结束，请检查终端2日志。"
  exit 1
fi
wait_for "确认多点导航已经结束" /navigate_through_poses/_action/status \
  action_msgs/msg/GoalStatusArray 'all(s.status in (4, 5, 6) for s in m.status_list)' \
  --qos-durability transient_local --qos-reliability reliable
SENTRY_COMPLETE=1
log "完整流程结束；请将遥控置于急停，再结束导航和行为树。"
