@echo off
REM Launch script for Ground Truth Annotator with OpenCV DLL path

REM Add OpenCV DLL directory to PATH
set OPENCV_PATH=C:\Dev_Libs\opencv\build\x64\vc16\bin
set PATH=%OPENCV_PATH%;%PATH%

REM Launch the annotator with provided arguments
"%~dp0build\bin\Release\ground_truth_annotator.exe" %*