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
export_prog_deps=[
  corelib_dep,
  boost_dep,
  ]
if cpp.get_id()=='msvc'
  export_prog_deps+=getopt_dep
endif


export_prog=executable(proj_tarname+'-export',
  'main.cc',
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  link_args: [],
  dependencies: [export_prog_deps],
  install: inst_full)

