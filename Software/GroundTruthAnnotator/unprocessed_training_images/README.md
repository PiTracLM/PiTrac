# Unprocessed Training Images

This directory contains new training images waiting to be annotated for golf ball detection.

## Usage

1. **Add images here**: Drop new cam2 strobed sequence images into this directory
2. **Run annotation tool**: `./run_annotator.bat unprocessed_training_images`
3. **Images get processed**: Annotated images are automatically moved to appropriate YOLO categories
4. **Train updated model**: Run `python yolo_training_workflow.py train` with new data

## Supported Image Types

The annotation tool auto-detects image types based on filename patterns:

- **cam2_shot_X_...** → `cam2_strobed_sequence` (primary training data)
- **...7strobe...** or **...dual...** → `dual_strobe` 
- **...10pulse...** or **...pulse...** → `multi_pulse`
- **...Xbit...** → `bit_pattern`

## What NOT to Include

- **Spin analysis images** (`spin_ball_X`) - Single ball close-ups for spin calculation
- **Calibration images** (`placed_ball`, `calibration`) - Single ball positioning shots  
- **Final detection results** (`final_found_ball`) - Post-processing output images

## Workflow

```bash
# 1. Add new images to this directory
cp /path/to/new_cam2_shots/*.png unprocessed_training_images/

# 2. Run annotation tool
./run_annotator.bat unprocessed_training_images

# 3. Train improved model
python yolo_training_workflow.py train --epochs 50

# 4. Test performance
python yolo_training_workflow.py test
```

## Current Dataset Status

After cleaning (removed spin analysis and single-ball images):
- **Focus**: Multi-ball cam2 strobed sequences only
- **Quality**: High-quality annotations for ball-in-flight detection
- **Purpose**: Improve PiTrac's core ball tracking capability