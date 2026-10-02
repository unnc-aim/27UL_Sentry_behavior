#!/usr/bin/env python3
"""检查条件日志、手动暂停重启和 SIGINT 退出。运行前加载 ROS 2 工作区环境。"""

import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

os.environ["ROS_DOMAIN_ID"] = "188"
os.environ["ROS_LOCALHOST_ONLY"] = "1"
output_root = Path(tempfile.mkdtemp(prefix="behavior_conditions_check_"))
os.environ["ROS_LOG_DIR"] = str(output_root / "ros")

import rclpy
from action_msgs.srv import CancelGoal
from dji_referee_protocol.msg import RobotHeat, RobotPerformance
from std_msgs.msg import Int32


def run_case(mode):
    directory = output_root / mode
    directory.mkdir()
    os.environ["ROS_LOG_DIR"] = str(directory / "ros")
    output_path = directory / "terminal.log"
    params = Path(__file__).resolve().parent / "params/sentry_behavior_conditions_test.yaml"
    node = rclpy.create_node("conditions_shutdown_check")
    hp = node.create_publisher(RobotPerformance, "/referee/common/robot_performance", 10)
    heat = node.create_publisher(RobotHeat, "/referee/common/robot_heat", 10)
    manual = node.create_publisher(Int32, "/manual_start", 10)

    def output():
        return re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", output_path.read_text())

    def wait_for(text, start=0):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
            if text in output()[start:]:
                return
        raise AssertionError(f"等待日志超时：{text}；日志 {output_path}")

    def publish(publisher, message, expected):
        start = len(output())
        publisher.publish(message)
        wait_for(expected, start)
        return output()[start:]

    with output_path.open("w") as stream:
        process = subprocess.Popen(
            ["ros2", "launch", "behavior", "pb2025_sentry_behavior_launch_new.py",
             f"params_file:={params}"],
            stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            wait_for("onGoalReceived with tree name 'competition_test_conditions'")
            deadline = time.monotonic() + 10
            while any(p.get_subscription_count() == 0 for p in (hp, heat, manual)):
                assert time.monotonic() < deadline, "等待订阅者超时"
                rclpy.spin_once(node, timeout_sec=0.05)
            hp.publish(RobotPerformance(current_hp=400, maximum_hp=400))
            heat.publish(RobotHeat(shooter_17mm_barrel_heat=0))
            publish(manual, Int32(data=1), "TrackHeat started: aim_allowed=1")

            if mode == "paused":
                for value, low in ((151, 0), (150, 1), (400, 0)):
                    publish(hp, RobotPerformance(current_hp=value, maximum_hp=400),
                            f"IsHpLow hp={value} threshold=150 hp_low={low}")
                for value in (239, 240, 100, 51, 50, 240, 100):
                    expected = (f"TrackHeat heat={value} aim_allowed={int(value == 50)}"
                                if value in (240, 50) else f"17mm_heat={value},")
                    chunk = publish(heat, RobotHeat(shooter_17mm_barrel_heat=value), expected)
                    if value in (100, 51):
                        assert "aim_allowed=1" not in chunk, chunk
                manual.publish(Int32(data=0))
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)
                start = len(output())
                deadline = time.monotonic() + 1.5
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)
                assert "[PrintRefereeStatus]" not in output()[start:]
                publish(manual, Int32(data=1), "TrackHeat started: aim_allowed=1")
                wait_for("17mm_heat=100,", start)
                manual.publish(Int32(data=0))
                deadline = time.monotonic() + 0.5
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.05)

            if mode == "canceled":
                cancel = node.create_client(CancelGoal, "/pb2025_sentry_behavior/_action/cancel_goal")
                assert cancel.wait_for_service(timeout_sec=10)
                future = cancel.call_async(CancelGoal.Request())
                rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                assert future.done()
                assert future.result().return_code == CancelGoal.Response.ERROR_NONE
                wait_for("Goal was canceled.")

            process.send_signal(signal.SIGINT)
            process.wait(timeout=10)
            text = output()
            assert process.returncode == 0, f"退出码 {process.returncode}；日志 {output_path}"
            for executable in ("behavior_server-1", "behavior_client-2"):
                assert f"[{executable}]: process has finished cleanly" in text, output_path
            assert "process has died" not in text
            assert "[ERROR]" not in text
            assert "NOT received" not in text
            assert "IDLE ->" not in text
            print(f"{mode}：通过；日志 {output_path}")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            node.destroy_node()


if __name__ == "__main__":
    rclpy.init()
    try:
        for case in ("paused", "running", "canceled"):
            run_case(case)
    finally:
        rclpy.shutdown()
