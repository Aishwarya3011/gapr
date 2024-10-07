win32_srcs=[]
if host_machine.system()=='windows'
  win32_srcs+=import('windows').compile_resources(
    corelib_ver_rc, corelib_dep_rc,
    args: [],
    depend_files: [corelib_ver_rc_deps, corelib_dep_rc_deps],
    depends: [],
    include_directories: [],
    )
endif
convert_prog_deps=[
  corelib_dep,
  boost_dep,
  boost_proc_dep,
  ssl_dep,
  thr_dep,
  tiff_dep,
  vpx_dep,
  zlib_dep,
  ]
if get_option('enable-av1').enabled()
  convert_prog_deps+=aom_dep
endif

if cpp.get_id()=='msvc'
  convert_prog_deps+=getopt_dep
endif

convert_prog=executable(proj_tarname+'-convert',
  'main.cc',
  'savewebm.cc',
  'split.cc',
  'split-plugin.cc',
  'loadtiff.cc',
  'tifftiled.cc',
  'dshelper.cc',
  'nethelper.cc',
  'decode-webm.cc',
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  cpp_args: ['-DBOOST_BEAST_USE_STD_STRING_VIEW'],
  link_args: [],
  dependencies: [convert_prog_deps],
  install: inst_full)

