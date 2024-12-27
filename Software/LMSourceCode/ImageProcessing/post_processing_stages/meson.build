rpicam_app_src += files([
    'hdr_stage.cpp',
    'histogram.cpp',
    'motion_detect_stage.cpp',
    'negate_stage.cpp',
    'post_processing_stage.cpp',
    'pwl.cpp',
])

post_processing_headers = files([
    'histogram.hpp',
    'object_detect.hpp',
    'post_processing_stage.hpp',
    'pwl.hpp',
    'segmentation.hpp',
    'tf_stage.hpp',
])

enable_opencv = get_option('enable_opencv')
opencv_dep = dependency('opencv4', required : false)
if enable_opencv and opencv_dep.found()
    rpicam_app_src += files([
        'sobel_cv_stage.cpp',
        'face_detect_cv_stage.cpp',
        'annotate_cv_stage.cpp',
        'plot_pose_cv_stage.cpp',
        'object_detect_draw_cv_stage.cpp',
    ])
    rpicam_app_dep += opencv_dep
else
    enable_opencv = false
endif

install_headers(post_processing_headers, subdir: meson.project_name() / 'post_processing_stages')
