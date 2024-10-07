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
map_prog_deps=[
  corelib_dep,
  boost_dep,
  boost_proc_dep,
  ]
if cpp.get_id()=='msvc'
  map_prog_deps+=getopt_dep
endif

map_prog=executable(proj_tarname+'-map',
  'main.cc',
  'decimate.cc',
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  link_args: [],
  dependencies: [map_prog_deps, thr_dep],
  install: inst_full)

