#!/usr/bin/env python3
"""Generate a printable ChArUco calibration board compatible with OpenCV 4.6+."""

import cv2


def get_opencv_version():
    """Parse OpenCV version into tuple of integers."""
    version = cv2.__version__
    major, minor, patch = version.split('.')[:3]
    return (int(major), int(minor), int(patch.split('-')[0]))


def is_new_api():
    """Check if OpenCV uses new API (>= 4.7.0)."""
    return get_opencv_version() >= (4, 7, 0)


def generate_charuco_board(
    squares_x=7,
    squares_y=10,
    square_length=0.025,  # 25mm squares
    marker_length=0.020,  # 20mm markers
    output_path="charuco_board_7x10.png",
    dict_type=cv2.aruco.DICT_4X4_50
):
    """Generate ChArUco calibration board with OpenCV version compatibility."""
    print(f"OpenCV version: {cv2.__version__}")
    print(f"Using {'NEW' if is_new_api() else 'OLD'} API")

    # Create ArUco dictionary with version compatibility
    if is_new_api():
        aruco_dict = cv2.aruco.getPredefinedDictionary(dict_type)
    else:
        aruco_dict = cv2.aruco.Dictionary_get(dict_type)

    # Create ChArUco board with version compatibility
    if is_new_api():
        board = cv2.aruco.CharucoBoard(
            (squares_x, squares_y),
            square_length,
            marker_length,
            aruco_dict
        )
    else:
        board = cv2.aruco.CharucoBoard_create(
            squares_x,
            squares_y,
            square_length,
            marker_length,
            aruco_dict
        )

    # Generate board image for A4 paper @ 300 DPI
    # A4 = 210mm x 297mm = 2480px x 3508px @ 300 DPI
    img_width = 2480
    img_height = 3508
    margin = 50  # pixels

    board_img = board.generateImage(
        (img_width, img_height),
        marginSize=margin
    )

    # Save the image
    cv2.imwrite(output_path, board_img)

    # Calculate physical dimensions
    board_width_mm = squares_x * square_length * 1000
    board_height_mm = squares_y * square_length * 1000

    print(f"\nChArUco board generated successfully!")
    print(f"Saved to: {output_path}")
    print(f"\nBoard Specifications:")
    print(f"   - Grid: {squares_x} x {squares_y} squares")
    print(f"   - Square size: {square_length*1000:.1f} mm")
    print(f"   - Marker size: {marker_length*1000:.1f} mm")
    print(f"   - Board dimensions: {board_width_mm:.1f} mm x {board_height_mm:.1f} mm")
    print(f"   - Image size: {img_width} x {img_height} pixels")
    print(f"   - ArUco dictionary: DICT_4X4_50")
    print(f"\nPrinting Instructions:")
    print(f"   1. Print on A4 paper (210mm x 297mm)")
    print(f"   2. Use 'Actual Size' or '100%' scale (NO fit-to-page)")
    print(f"   3. Use high-quality printer settings")
    print(f"   4. Mount on flat, rigid cardboard")
    print(f"   5. Verify printed square size with ruler: {square_length*1000:.1f} mm")

    return board


if __name__ == "__main__":
    # Generate default ChArUco board for PiTrac
    board = generate_charuco_board(
        squares_x=7,
        squares_y=10,
        square_length=0.025,  # 25mm
        marker_length=0.020,  # 20mm
        output_path="charuco_board_7x10.png"
    )

    print("\nBoard generation complete!")
    print("Next steps:")
    print("   1. Print charuco_board_7x10.png")
    print("   2. Mount on cardboard for rigidity")
    print("   3. Use in PiTrac web UI: Calibration > Lens Distortion Calibration")
    print("   4. System will auto-capture 20 images with quality validation")
