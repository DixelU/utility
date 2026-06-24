import cv2
import numpy as np

# Define the width of the image (known)
width = 320

# Open the raw file in binary mode
try:
    with open('D:\\Download\\Downloads\\Win7RescuePE.iso', 'rb') as f:
        data = np.frombuffer(f.read(), dtype=np.uint8)
except Exception as e:
    raise IOError(f"Failed to read file: {e}")

# Check if data is not empty and length is as expected
if len(data) == 0:
    raise ValueError("The file is empty or not correctly loaded.")

# Calculate the number of full rows
full_rows = len(data) // (width * 3)
print(f"Total rows {full_rows}")
# full_rows = cap # OVERRIDE HERE

# Calculate the total number of pixels that make up full rows
valid_data_length = full_rows * width * 3

# Truncate the data to only include full rows
valid_data = data[:valid_data_length]

# Ensure the data length matches the expected full rows * width
if len(valid_data) != valid_data_length:
    raise ValueError("Data truncation failed; dimensions may not be as expected.")

# Reshape the valid data into the correct shape (full_rows, width)
try:

    image_data = valid_data.reshape((full_rows, width, 3))
except ValueError as e:
    raise ValueError(f"Error reshaping data: {e}")

# Debugging: Display the first few lines to ensure data looks correct
# print("First few pixels:", image_data[:5, :10])  # Show first few pixels of the first few rows

cl_width = width
cl_height = width
x = 0
y = 0
step = cl_height // 16

while True:

        roi = image_data[y:y + cl_height, x:x + cl_width]
        #big_letters = ((roi > 0x41) & (roi < 0x5a)).astype(np.uint8)
        #small_letters = ((roi > 0x61) & (roi < 0x7a)).astype(np.uint8)
        #is_text = big_letters * 0xFF + small_letters * 0xA0

        #cv2.imshow("is_text", is_text)
        cv2.imshow("ATOP", roi)

        result = cv2.waitKey(0)  # Wait for a key press

        if result == 0x77 and y >= step:
                y -= step
        if result == 0x61 and x >= step:
                x -= step
        if result == 0x64 and x < width - cl_width - step:
                x += step
        if result == 0x73 and y < full_rows - cl_height - step:
                y += step
        if result == 0x71 and step > 1:
                step //= 2
        if result == 0x65:
                step *= 2

        if result == 0x20:
                break

        print(f"({x}, {y}), byte {x + y * width:08x} ~ {(x + y * width) * 100 / len(data):.2f}%, step {step}")