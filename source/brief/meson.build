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
brief_prog_deps=[
  corelib_dep,
  boost_dep,
  omp_dep,
  ]
if cpp.get_id()=='msvc'
  brief_prog_deps+=getopt_dep
endif


brief_prog=executable(proj_tarname+'-brief',
  'main.cc',
  'f1s.cc',
  'dist.cc',
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  link_args: [],
  dependencies: [brief_prog_deps],
  install: inst_full)

