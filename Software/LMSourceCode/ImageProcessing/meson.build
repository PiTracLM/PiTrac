# SPDX-License-Identifier: GPL-2.0-only 
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#


project('pitrac_lm', 'c', 'cpp',
    meson_version : '>= 0.61',
    version : '0.0.1',
    default_options : [
        'werror=true',
        'warning_level=2',
        'cpp_std=c++20',
    ],
    license : 'LGPL 2.1+')

add_global_arguments('-Wno-deprecated-enum-enum-conversion', language : 'cpp')
add_global_arguments('-Wno-deprecated-declarations', language : 'cpp')
add_global_arguments('-Wno-comment', language : 'cpp')
add_global_arguments('-Wno-unused', language : 'cpp')
add_global_arguments('-Wno-error', language : 'cpp')
add_global_arguments('-D_FILE_OFFSET_BITS=64', language : 'cpp')
add_global_arguments('-DLIBCXX_ENABLE_INCOMPLETE_FEATURES=ON', language : 'cpp')
add_global_arguments('-DBOOST_LOG_DYN_LINK', language : 'cpp')
add_global_arguments('-DBOOST_BIND_GLOBAL_PLACEHOLDERS', language : 'cpp')


fs = import('fs')

# TBD FIX - The following may be redundant of the above options
cpp_arguments = ['-pedantic', '-Wno-unused-parameter', '-faligned-new', '-DBOOST_LOG_DYN_LINK', '-Wno-error', '-Wno-deprecated-enum-enum-conversion', '-Wno-decprecated-declarations']

# Needed for file sizes > 32-bits.
cpp_arguments += '-D_FILE_OFFSET_BITS=64'

# We need a relatively recent version of gcc that will support c++20

cxx = meson.get_compiler('cpp')
cpu = host_machine.cpu()

if cxx.get_id() == 'gcc'
    cpp_arguments += '-Wno-psabi'
endif

if cpu == 'aarch64' or neon == 'arm64'
    cpp_arguments += '-ftree-vectorize'
elif neon == 'armv8-neon'
    cpp_arguments += ['-mfpu=neon-fp-armv8', '-ftree-vectorize']
endif

ssl_dep = dependency('openssl', required : true)
activemq_dep = dependency('activemq-cpp', required : true)
apr_dep = cxx.find_library('apr-1', required : true)

libcamera_dep = dependency('libcamera', required : true)
thread_dep = dependency('threads', required : true)
opencv_dep = dependency('opencv4', required : true, version : '>=4.9.0', include_type: 'system',)

lgpio_dep = dependency('lgpio', required : true)
fmt_dep = dependency('fmt', required : true)

boost_dep = dependency('boost', method: 'pkg-config', version : '>=1.74.0', modules : ['timer', 'log', 'thread', 'filesystem', 'regex'])

msgpack_dep = dependency('msgpack-cxx', required : true)
yamlcpp_dep = dependency('yaml-cpp', required : true)

rpicam_app_src = []
rpicam_app_dep = [libcamera_dep, lgpio_dep]


pitrac_lm_module_deps = [
	libcamera_dep, thread_dep, opencv_dep, lgpio_dep, rpicam_app_dep, 
	fmt_dep, boost_dep, activemq_dep, ssl_dep, apr_dep, msgpack_dep, yamlcpp_dep,]



subdir('core')
subdir('encoder')
subdir('image')
subdir('output')
subdir('preview')
subdir('post_processing_stages')


# Generate a version string.
version_cmd = [meson.project_source_root() / 'utils' / 'version.py', meson.project_version()]

# Check if a version.gen file is present.
# This would have been generated from the meson dist command.
dist_version_file = meson.project_source_root() / 'version.gen'
if fs.is_file(dist_version_file)
    version_cmd += fs.read(dist_version_file)
endif

version_cpp = vcs_tag(command : version_cmd,
                      replace_string: '@VER@',
                      input : meson.project_source_root() / 'core' / 'version.cpp.in',
                      output : 'version.cpp',
                      fallback : meson.project_version())

rpicam_app_src += version_cpp


enable_recompile_closed_source = get_option('enable_recompile_closed_source')
enable_compile_on_pi4 = get_option('enable_compile_on_pi4')

# TBD - This is mostly deprecated.  Delete soon!
if enable_compile_on_pi4
  message('Compiling on a Pi 4')
  add_global_arguments('-DPITRAC_COMPILING_ON_PI_4', language : 'cpp')
else
  message('Compiling on a Pi 5')
endif

closed_source_object_files = []
pitrac_lm_sources = []

if enable_recompile_closed_source 
    message('Recompiling closed source code')
    run_command('cp', '/home/USERNAME/Dev/ClosedSourceCode/gs_e6_response.cpp.FULL', 'gs_e6_response.cpp', check:true)
    pitrac_lm_sources += ([ 'gs_e6_response.cpp' ])
endif

if enable_recompile_closed_source 
    message('Recompiling closed source code')
    run_command('cp', '/home/USERNAME/Dev/ClosedSourceCode/obfuscate.h', 'obfuscate.h', check:true)
    pitrac_lm_sources += ([ 'gs_e6_response.cpp' ])
endif

# Move the pre-compiled, closed-source object file(s) into place
# This is a workaround to the fact that meson will not allow paths in the 
# output and input parameters of a custom_target
create_closed_source_objects = find_program('create_closed_source_objects.sh')
closed_source_target = custom_target('copy_closed_source_object',
    input : 'gs_e6_results.cpp',
    output : 'gs_e6_response.cpp.o',
    command : [ create_closed_source_objects, '@INPUT@', '@OUTPUT@'],
)

if not enable_recompile_closed_source 
    message('Using pre-compiled closed object_code')
    pitrac_lm_sources += ([ closed_source_target, ])
    closed_source_object_files += (['gs_e6_response.cpp.o'])
endif

if not enable_recompile_closed_source 
    r = run_command ( 'cp', '-f', './ClosedSourceObjectFiles/gs_e6_response.cpp.o', '.', check:true)
endif

libav_dep_names = ['libavcodec', 'libavdevice', 'libavformat', 'libavutil', 'libswresample']
libav_deps = []

pitrac_lm_sources += ([
                        'ED.cpp',
                        'EDColor.cpp',
                        'EDPF.cpp',
			'lm_main.cpp',
			'gs_globals.cpp',
			'gs_fsm.cpp',
			'libcamera_interface.cpp',
			'libcamera_jpeg.cpp',
			'ball_watcher.cpp',
			'ball_watcher_image_buffer.cpp',
			'libcamera_jpeg.cpp',
			'ball_image_proc.cpp',
			'pulse_strobe.cpp',
			'colorsys.cpp',
			'cv_utils.cpp',
			'EllipseDetectorCommon.cpp',
			'EllipseDetectorYaed.cpp',
			'golf_ball.cpp',
			'gs_automated_testing.cpp',
			'gs_camera.cpp',
			'gs_web_api.cpp',
			'gs_clubs.cpp',
			'gs_club_data.cpp',
			'gs_options.cpp',
			'gs_config.cpp',
			'configuration_manager.cpp',
			'gs_sim_interface.cpp',
			'gs_gspro_interface.cpp',
			'gs_gspro_response.cpp',
			'gs_gspro_test_server.cpp',
			'gs_sim_socket_interface.cpp',
                        'gs_e6_interface.cpp',
                        'gs_e6_results.cpp',
			'logging_tools.cpp',
			'gs_events.cpp',
			'worker_thread.cpp',
			'camera_hardware.cpp',
                        'gs_ipc_message.cpp',
                        'gs_ipc_control_msg.cpp',
                        'gs_results.cpp',
                        'gs_gspro_results.cpp',
                        'gs_ui_system.cpp',
                        'gs_ipc_mat.cpp',
                        'gs_ipc_result.cpp',
                        'gs_ipc_test.cpp',
                        'gs_ipc_system.cpp',
                        'gs_message_consumer.cpp',
                        'gs_message_producer.cpp',
])

pitrac_lm_sources += ([
			rpicam_app_src,
])


pitrac_lm_include_dirs = ([ 
		'.', 
		'/usr/include/apr-1.0',
])



pitrac_lm_module_deps += ([
			rpicam_app_dep,
])

exec = executable('pitrac_lm', 
	pitrac_lm_sources,
	include_directories : pitrac_lm_include_dirs,
	install : true,
        objects : closed_source_object_files,
	dependencies : pitrac_lm_module_deps
	)

# Hacky two targets, because can't figure out how to execute more than one command
# per  target.  TBD
custom_target('post_build1',
  depends: exec,  # Ensure the executable is built first
  input: exec,
  output: 'dummy_file1',  # NOTE - command will execute in the build directory
  command: [ 'cp', '-f', './pitrac_lm.p/gs_e6_response.cpp.o', '../ClosedSourceObjectFiles'],
  build_by_default : true
)

custom_target('post_build2',
  depends: exec,  # Ensure the executable is built first
  input: exec,
  output: 'dummy_file2',  # NOTE - command will execute in the build directory
  command: [ 'cp', '-f', './pitrac_lm.p/gs_e6_response.cpp.o', '..'],
  build_by_default : true
)

