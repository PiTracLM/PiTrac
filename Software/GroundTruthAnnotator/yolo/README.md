# PiTrac Golf Ball YOLO Dataset

## Dataset Statistics
- Total Images: 38
- Total Balls: 211
- Average Balls per Image: 5.6

## Breakdown by Strobe Type
- **multi_pulse**: 7 images, 60 balls
- **dual_strobe**: 2 images, 13 balls
- **unknown**: 5 images, 14 balls
- **strobed_sequence**: 22 images, 122 balls
- **spin_analysis**: 2 images, 2 balls

## Directory Structure
```
yolo/
├── images/
│   ├── strobed_sequence/    # Main camera 2 strobed ball sequences
│   ├── dual_strobe/         # Dual strobe patterns
│   ├── multi_pulse/         # Multi-pulse patterns
│   ├── bit_pattern/         # Bit-encoded strobe patterns
│   ├── spin_analysis/       # Spin analysis images
│   └── unknown/            # Unclassified images
├── labels/                  # Corresponding YOLO format annotations
│   └── [same structure as images]
├── golf_balls.yaml         # YOLO dataset configuration
└── README.md              # This file
```

## YOLO Format
Each .txt file contains one line per golf ball:
`class_id center_x center_y width height`

Where all coordinates are normalized (0.0 to 1.0) and class_id=0 for golf balls.
