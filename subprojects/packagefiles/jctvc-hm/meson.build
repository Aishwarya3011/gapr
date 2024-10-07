project('libhevc', 'cpp')

cflags='-O3 -Wuninitialized -fPIC'
if meson.get_compiler('c').get_id()=='gcc'
  cflags+=' -Wno-error=class-memaccess'
  cflags+=' -Wno-error=array-bounds'
  cflags+=' -Wno-error=maybe-uninitialized'
endif
prog_make=find_program('make', required: true)
build_cmd=[prog_make, '-j', 'release', 'MM32=0', 'HIGHBITDEPTH=1',
  #'CPP='+' '.join(meson.get_compiler('cpp').cmd_array()+['-fPIC']),
  #'CC='+' '.join(meson.get_compiler('c').cmd_array()+['-fPIC']),
  'RELEASE_CPPFLAGS='+cflags,
  'RELEASE_CFLAGS='+cflags,
  'LIB_DIR='+meson.current_build_dir(),
  'BIN_DIR='+meson.current_build_dir()/'bin',
  'OBJ_DIR='+meson.current_build_dir()/'obj',
  'CPP='+' '.join(meson.get_compiler('cpp').cmd_array()),
  'CC='+' '.join(meson.get_compiler('c').cmd_array()),
  '-C',
]
libhevc_a=custom_target(meson.project_name()+'-make',
  console: true,
  command: build_cmd+[meson.current_source_dir()/'build'/'linux'/'lib'/'TLibCommon'],
  depend_files: [meson.current_source_dir()/'build'/'linux'/'makefile'],
  output: 'libTLibCommonHighBitDepthStatic.a',
)
libhevc_b=custom_target(meson.project_name()+'-make2',
  console: true,
  command: build_cmd+[meson.current_source_dir()/'build'/'linux'/'lib'/'TLibDecoder'],
  depends: [libhevc_a],
  depend_files: [meson.current_source_dir()/'build'/'linux'/'makefile'],
  output: 'libTLibDecoderHighBitDepthStatic.a',
)

libhevc=shared_library('hevc-jctvc-hm',
  link_whole: [libhevc_a, libhevc_b],
  name_prefix: 'lib',
  install: true,
)

libhevc_dep=declare_dependency(
  include_directories: include_directories('source/Lib'),
  link_with: libhevc,
)

