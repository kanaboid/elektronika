"""Read COM port for a few seconds (no TTY required). Default COM5 matches typical XIAO USB-CDC."""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial not installed in this Python", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM5")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--seconds", type=float, default=7.0)
    args = p.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.4)
    ser.reset_input_buffer()
    end = time.time() + args.seconds
    buf = bytearray()
    while time.time() < end:
        buf.extend(ser.read(4096))
    ser.close()
    sys.stdout.write(buf.decode("utf-8", errors="replace"))


if __name__ == "__main__":
    main()
