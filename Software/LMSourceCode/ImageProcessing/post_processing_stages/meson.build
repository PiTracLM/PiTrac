posproc_libdir = get_option('prefix') / get_option('libdir') / 'rpicam-apps-postproc'

conf_data.set('POSTPROC_LIB_DIR', '"' + posproc_libdir + '"')
configure_file(output : 'postproc_lib.h', configuration : conf_data)

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

    opencv_postproc_src = files([
        'sobel_cv_stage.cpp',
        'face_detect_cv_stage.cpp',
        'annotate_cv_stage.cpp',
        'plot_pose_cv_stage.cpp',
        'object_detect_draw_cv_stage.cpp',
    ])


    opencv_postproc_lib = shared_module('opencv-postproc', opencv_postproc_src,
                                        include_directories : '../',
                                        dependencies : [libcamera_dep, opencv_dep],
                                        cpp_args : cpp_arguments,
                                        install : true,
                                        install_dir : posproc_libdir,
                                        name_prefix : '',
                                       )

else
    enable_opencv = false
endif


post_processing_headers = files([
    'post_processing_stage.hpp',
])


install_headers(post_processing_headers, subdir: meson.project_name() / 'post_processing_stages')
