#!/usr/bin/env python3
import argparse
import re
import threading
import time
from typing import Optional

import rclpy
from rclpy.node import Node
from std_msgs.msg import Int16MultiArray

import serial

LINE_PATTERN = re.compile(r"^L:(-?\d+),R:(-?\d+)\s*$")


class SerialBridge(Node):
    def __init__(self, port: str, baud: int, topic: str, reconnect_sec: float, log_raw: bool) -> None:
        super().__init__("atomjoy_serial_bridge")
        self._port = port
        self._baud = baud
        self._reconnect_sec = reconnect_sec
        self._log_raw = log_raw
        self._serial: Optional[serial.Serial] = None
        self._stop_event = threading.Event()
        self._publisher = self.create_publisher(Int16MultiArray, topic, 10)

        self._thread = threading.Thread(target=self._serial_loop, daemon=True)
        self._thread.start()

    def destroy_node(self) -> bool:
        self._stop_event.set()
        self._close_serial()
        return super().destroy_node()

    def _close_serial(self) -> None:
        if self._serial is None:
            return
        try:
            self._serial.close()
        except serial.SerialException:
            pass
        self._serial = None

    def _connect(self) -> bool:
        try:
            self._serial = serial.Serial(self._port, self._baud, timeout=0.1)
            self.get_logger().info(f"Connected to {self._port}")
            return True
        except serial.SerialException as exc:
            self.get_logger().warn(f"Serial connect failed: {exc}")
            self._serial = None
            return False

    def _serial_loop(self) -> None:
        while not self._stop_event.is_set() and rclpy.ok():
            if self._serial is None:
                if not self._connect():
                    time.sleep(self._reconnect_sec)
                    continue
            try:
                line_bytes = self._serial.readline()
            except serial.SerialException as exc:
                self.get_logger().warn(f"Serial read failed: {exc}")
                self._close_serial()
                time.sleep(self._reconnect_sec)
                continue

            if not line_bytes:
                continue

            try:
                line = line_bytes.decode("utf-8", errors="ignore").strip()
            except UnicodeDecodeError:
                continue

            if self._log_raw:
                self.get_logger().info(f"raw: {line}")

            match = LINE_PATTERN.match(line)
            if not match:
                continue

            left = int(match.group(1))
            right = int(match.group(2))

            msg = Int16MultiArray()
            msg.data = [left, right]
            self._publisher.publish(msg)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bridge AtomJoy serial commands to ROS 2 topic.")
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate (USB CDC ignores this)")
    parser.add_argument("--topic", default="/atomjoy/drive_cmd", help="ROS 2 topic name")
    parser.add_argument("--reconnect", type=float, default=2.0, help="Reconnect interval seconds")
    parser.add_argument("--log-raw", action="store_true", help="Log every raw serial line")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    rclpy.init()
    node = SerialBridge(args.port, args.baud, args.topic, args.reconnect, args.log_raw)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
