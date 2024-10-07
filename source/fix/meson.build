subdir('translations')

cur_prog_mocs=import('qt5').preprocess(
  moc_headers: [
	 'dialog.hh',
	 'dialog-alt.hh',
	 'canvas.hh',
	 'window.hh',
	 'neuron-list.hh',
  ],
  moc_sources: [
  ],
  ui_files: [
    'ui/win-main.ui',
    'ui/dlg-quality.ui',
    'ui/dlg-channels.ui',
    'ui/dlg-property.ui',
    'ui/dlg-create-neuron.ui',
    'ui/dlg-rename-neuron.ui',
    'ui/dlg-select-type.ui',
    'ui/dlg-error-state.ui',
    '../proofread/ui/dlg-node-state.ui',
  ],
  qresources: ['fix.qrc'],
  include_directories: [],
  dependencies: [],
)

cur_prog_srcs=[
  'compute.cc',
  'window.cc',
  'main.cc',
  'misc.cc',
  'neuron-list.cc',
]
if host_machine.system()=='windows'
  cur_prog_srcs+=import('windows').compile_resources(
    corelib_ver_rc, guilib_dep_rc, guilib_icon_rc,
    depend_files: [corelib_ver_rc_deps, guilib_dep_rc_deps, guilib_icon_rc_deps],
  )
endif

fix_prog=executable(proj_tarname+'-fix',
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
  install: inst_full)

if false
  fix_prog_gtk=executable(proj_tarname+'-fix-gtk',
	 'main-gtk.cc',
	 'window-gtk.cc',
	 'model.cc',
	 'compute.cc',
	 'misc.cc',
	 import('gnome').compile_resources('fix-resources', 'gresource.xml', c_name: 'fix'),
	 include_directories: [proj_inc, include_directories('../guilib')],
	 cpp_args: ['-DWITH_GTK', '-w'],
	 dependencies: [
		corelib_dep,
		thr_dep,
		boost_dep,
		#dependency('gio-2.0', version: '>= 2.50'),
		dependency('gtk+-3.0', version: '>= 3.22'),
		dependency('epoxy'),
		],
	 )
endif
