start_prog_deps=[
  corelib_dep,
  guilib_dep,
  qt5_dep,
  ]
if cpp.get_id()=='getopt'
  start_prog_deps+=getopt_dep
endif

qt5=import('qt5')
start_prog_mocs=qt5.preprocess(
  moc_headers: [
	 'start-window.hh',
	 ],
  moc_sources: [
	 'start-window.cc',
	 ],
  ui_files: [
	 'ui/win-start.ui',
	 ],
  include_directories: [],
  dependencies: [],
  )
subdir('translations')

win32_srcs=[]
if host_machine.system()=='windows'
  win32_srcs+=import('windows').compile_resources(
	 corelib_ver_rc, guilib_dep_rc, guilib_icon_rc,
	 args: [],
	 depend_files: [corelib_ver_rc_deps, guilib_dep_rc_deps, guilib_icon_rc_deps],
	 depends: [],
	 include_directories: [],
	 )
endif


start_prog=executable(proj_tarname,
  'main.cc',
  'start-window.cc',
  start_prog_mocs,
  win32_srcs,
  win_subsystem: 'windows',
  include_directories: [proj_inc],
  cpp_args: [], #-DXXX_COMPILATION
  link_args: [],
  dependencies: [
	 start_prog_deps,
	 getopt_dep
	 ###########################
	 ],
  link_with: [],
  install: inst_full)

