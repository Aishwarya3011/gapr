subdir('translations')

cur_prog_mocs=import('qt5').preprocess(
  moc_headers: [
      'canvas.hh',
      'dialog-alt.hh',
      'window.hh',
	 ],
  moc_sources: [
      'window.cc',
	 ],
  ui_files: [
      'ui/win-main.ui',
      'ui/dlg-display.ui',
      'ui/dlg-node-state.ui',
      'ui/dlg-error-state.ui',
	 ],
  qresources: ['../fix/fix.qrc'],
  include_directories: [],
  dependencies: [],
  )

cur_prog_srcs=[
  '../fix/compute.cc',
  '../fix/misc.cc',
  'canvas.cc',
  'window.cc',
  'main.cc',
]
if host_machine.system()=='windows'
  cur_prog_srcs+=import('windows').compile_resources(
	 corelib_ver_rc, guilib_dep_rc, guilib_icon_rc,
	 depend_files: [corelib_ver_rc_deps, guilib_dep_rc_deps, guilib_icon_rc_deps],
	 )
endif

proofread_prog=executable(proj_tarname+'-proofread',
  cur_prog_srcs,
  cur_prog_mocs,
  win_subsystem: 'windows',
  include_directories: [proj_inc],
  cpp_args: [],
  link_args: [],
  dependencies: [
      corelib_dep,
      guilib_dep,
      getopt_dep,
      boost_dep,
      qt5_dep,
    ],
  link_with: [],
  install: inst_lite)

