# ROS 2 Serial Bridge

## Overview
This project emits left/right wheel commands over USB serial. The script below
bridges that serial stream to a ROS 2 topic on your host PC.

## Requirements
- ROS 2 installed (Humble/Iron/etc.)
- Python packages: `rclpy`, `pyserial` (usually available in your ROS 2 Python)

## Usage
1. Connect the device by USB and complete calibration.
2. Run the bridge script from this example directory:

```
python3 tools/ros2_serial_bridge.py --port /dev/ttyACM0
```

## Published Topic
- Topic: `/atomjoy/drive_cmd`
- Message type: `std_msgs/msg/Int16MultiArray`
- Payload: `[left, right]`

## Notes
- The serial stream is line-based (`L:<left>,R:<right>`). The bridge ignores
  any line that does not match this format.
- USB CDC ignores baudrate; the `--baud` option is kept for convenience.
- Use `--log-raw` to print every received line for debugging.
