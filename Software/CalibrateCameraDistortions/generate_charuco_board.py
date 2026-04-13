#!/usr/bin/env python3
"""Generate a printable ChArUco calibration board compatible with OpenCV 4.6+."""

import cv2
import numpy as np


DPI = 300
PX_PER_MM = DPI / 25.4

A4_WIDTH_MM = 210
A4_HEIGHT_MM = 297
A4_WIDTH_PX = round(A4_WIDTH_MM * PX_PER_MM)
A4_HEIGHT_PX = round(A4_HEIGHT_MM * PX_PER_MM)


def get_opencv_version():
    version = cv2.__version__
    major, minor, patch = version.split('.')[:3]
    return (int(major), int(minor), int(patch.split('-')[0]))


def is_new_api():
    return get_opencv_version() >= (4, 7, 0)


def generate_charuco_board(
    squares_x=8,
    squares_y=11,
    square_length=0.023,
    marker_length=0.017,
    output_path="charuco_board_8x11.png",
    dict_type=cv2.aruco.DICT_4X4_50
):
    print(f"OpenCV version: {cv2.__version__}")
    print(f"Using {'NEW' if is_new_api() else 'OLD'} API")

    if is_new_api():
        aruco_dict = cv2.aruco.getPredefinedDictionary(dict_type)
    else:
        aruco_dict = cv2.aruco.Dictionary_get(dict_type)

    if is_new_api():
        board = cv2.aruco.CharucoBoard(
            (squares_x, squares_y),
            square_length,
            marker_length,
            aruco_dict
        )
    else:
        board = cv2.aruco.CharucoBoard_create(
            squares_x, squares_y,
            square_length, marker_length,
            aruco_dict
        )

    square_mm = square_length * 1000
    square_px = round(square_mm * PX_PER_MM)
    board_width_px = squares_x * square_px
    board_height_px = squares_y * square_px

    actual_square_mm = square_px / PX_PER_MM
    board_width_mm = squares_x * actual_square_mm
    board_height_mm = squares_y * actual_square_mm

    if board_width_px > A4_WIDTH_PX or board_height_px > A4_HEIGHT_PX:
        raise ValueError(
            f"Board ({board_width_mm:.1f} x {board_height_mm:.1f} mm) "
            f"exceeds A4 ({A4_WIDTH_MM} x {A4_HEIGHT_MM} mm). "
            f"Reduce square_length or grid dimensions.")

    board_img = board.generateImage((board_width_px, board_height_px), marginSize=0)

    canvas = np.ones((A4_HEIGHT_PX, A4_WIDTH_PX), dtype=np.uint8) * 255
    x_off = (A4_WIDTH_PX - board_width_px) // 2
    y_off = (A4_HEIGHT_PX - board_height_px) // 2
    canvas[y_off:y_off + board_height_px, x_off:x_off + board_width_px] = board_img

    cv2.imwrite(output_path, canvas)

    print(f"\nChArUco board generated successfully!")
    print(f"Saved to: {output_path}")
    print(f"\nBoard Specifications:")
    print(f"   Grid:            {squares_x} x {squares_y} squares")
    print(f"   Square size:     {actual_square_mm:.2f} mm ({square_px} px at {DPI} DPI)")
    print(f"   Marker size:     {marker_length*1000:.1f} mm")
    print(f"   Board size:      {board_width_mm:.1f} mm x {board_height_mm:.1f} mm")
    print(f"   Canvas:          {A4_WIDTH_PX} x {A4_HEIGHT_PX} px (A4 @ {DPI} DPI)")
    print(f"   Board offset:    ({x_off}, {y_off}) px from top-left")
    print(f"   Dictionary:      DICT_4X4_50")
    print(f"\nPrinting Instructions:")
    print(f"   1. Print on A4 paper (210 x 297 mm)")
    print(f"   2. Use 'Actual Size' or '100%' scale (NO fit-to-page)")
    print(f"   3. Use high-quality printer settings")
    print(f"   4. Mount on flat, rigid surface (glass or aluminum preferred)")
    print(f"   5. VERIFY with a ruler: each square must be {actual_square_mm:.1f} mm")

    return board


if __name__ == "__main__":
    board = generate_charuco_board(
        squares_x=8,
        squares_y=11,
        square_length=0.023,
        marker_length=0.017,
        output_path="charuco_board_8x11.png"
    )
