import argparse
import cv2
import numpy as np

parser = argparse.ArgumentParser("closeup_rtsp")
parser.add_argument("rtsp", help="RTSP stream URL.", type=str)
arg_obj = parser.parse_args()

cap = cv2.VideoCapture(arg_obj.rtsp, cv2.CAP_FFMPEG)

if not cap.isOpened():
    print('Cannot open RTSP stream')
    exit(-1)

x = 0
y = 0
step = 0
cl_height = 720
cl_width = 1280
step += 10
scale = 1

width  = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)

while True:
    grab, frame = cap.read()

    roi = frame[int(y / scale):int((y + cl_height) / scale), int(x / scale): int((x + cl_width) / scale)]
    roi = cv2.resize(roi, (cl_width, cl_height), interpolation=cv2.INTER_AREA)

    # frame = cv2.resize(frame, (int(width * scale), int(height * scale)), interpolation=cv2.INTER_CUBIC)
    # roi = frame[y:y + cl_height, x:x + cl_width]

    cv2.imshow("view", roi)

    result = cv2.waitKey(1)  # Wait for a key press

    if result == 0x77 and y >= step:
        y -= int(step * scale)
    if result == 0x61 and x >= step:
        x -= int(step * scale)
    if result == 0x64 and x < width * scale - cl_width - step:
        x += int(step * scale)
    if result == 0x73 and y < height * scale - cl_height - step:
        y += int(step * scale)
    if result == 0x71 and step > 1:
        step //= 2
    if result == 0x65:
        step *= 2

    if result == 0x2b:
        scale *= 1.1
        x = x * 1.1 + 0.05 * cl_width / (scale / 1.1)
        y = y * 1.1 + 0.05 * cl_height / (scale / 1.1)
    if result == 0x2d and scale >= 1.099999:
        scale /= 1.1
        x = x / 1.1 - 0.05 * cl_width / (scale * 1.1)
        y = y / 1.1 - 0.05 * cl_height / (scale * 1.1)
        if x < 0:
            x = 0
        if y < 0:
            y = 0

    if result == 0x20:
        break

    if result >= 0x00:
        print(f"R({result:02x}) ~ ({x}, {y}), step {step}")