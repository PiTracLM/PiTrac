cmake_minimum_required(VERSION 3.16)
project(GroundTruthAnnotator)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set OpenCV directory for your system
set(OpenCV_DIR "C:/Dev_Libs/opencv/build/x64/vc16" CACHE PATH "OpenCV directory")
find_package(OpenCV REQUIRED)

# Include directories
include_directories(${OpenCV_INCLUDE_DIRS})

# Add executables
add_executable(ground_truth_annotator ground_truth_annotator.cpp)

# Link libraries
target_link_libraries(ground_truth_annotator ${OpenCV_LIBS})

# For Windows, we'll use simple JSON without external library dependency
if(WIN32)
    target_compile_definitions(ground_truth_annotator PRIVATE SIMPLE_JSON)
endif()

# Set output directory
set_target_properties(ground_truth_annotator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)