if not get_option('install-type').startswith('dist-')
  error('included only if packaging')
endif
dist_type=get_option('install-type').split('-')[1]

flatpak_cmd=find_program('flatpak', required: false)

if flatpak_cmd.found()

  flatpak_build=custom_target('flatpak-build',
	 build_by_default: false, console: true, install: false,
	 depend_files: ['build-flatpak.sh'],
	 depends: [],
	 output: ['gapr-show.flatpak'],
	 command: ['sh', meson.current_source_dir()/'build-flatpak.sh',
		meson.current_source_dir()/'..'])

endif

gdk_pixbuf_dep=dependency('gdk-pixbuf-2.0', required: true)
gdk_pixbuf_dir=gdk_pixbuf_dep.get_variable(pkgconfig: 'gdk_pixbuf_moduledir')
gio_dep=dependency('gio-2.0', required: true)
gio_module_dir=gio_dep.get_variable(pkgconfig: 'giomoduledir')

qmake_more_names=[]
sys_root_dir=''
if meson.is_cross_build()
  sys_root_dir=meson.get_external_property('root')
  dump_res=run_command(meson.get_compiler('c'), '-dumpmachine', check: true)
  qmake_pfx=dump_res.stdout().strip()
  qmake_more_names+=qmake_pfx+'-qmake-qt5'
  qmake_more_names+=qmake_pfx+'-qmake'
endif

qmake_prg=find_program(qmake_more_names, 'qmake-qt5', 'qmake', required: true,
  dirs: [qt5_dep.get_variable('host_bins')])
qt_plugins_dir=run_command(qmake_prg, '-query', 'QT_INSTALL_PLUGINS', check: true).stdout().strip()
# XXX
gtk_binary_ver='3.0.0'
im_module_dir=sys_root_dir/'lib'/'gtk-3.0'/gtk_binary_ver/'immodules'

generic_inst_opts=[
  'dist_type='+dist_type,
  'bindir='+get_option('bindir'),
  'libdir='+get_option('libdir'),
  'qt_plugin_dir='+qt_plugins_dir,
  'gdk_pixbuf_dir='+gdk_pixbuf_dir,
  'gio_module_dir='+gio_module_dir,
  'im_module_dir='+im_module_dir,
  ]

if host_machine.system()=='linux'
  linux_inst_opts=generic_inst_opts
  linux_inst_opts+='libs_bl='+meson.current_source_dir()/'libs-freedesktop'

  foreach dirfix: [['bin', 'bin'], ['lib', 'lib'], ['share', 'data']]
    dir=get_option('prefix')/get_option(dirfix[1]+'dir')
    if dir!='/'+dirfix[0]
      meson.add_install_script('sh', '-c',
	'ln -sf -T ".$0" "$DESTDIR/$1"', dir, dirfix[0])
    endif
  endforeach

  meson.add_install_script('fix-bundle-linux.py', linux_inst_opts)
endif

if host_machine.system()=='windows'
  win_inst_opts=generic_inst_opts
  if meson.is_cross_build()
	 win_inst_opts+='sys_root='+sys_root_dir
	 win_inst_opts+='objdump='+find_program(qmake_pfx+'-objdump', required: true).full_path()
  endif

  meson.add_install_script('fix-bundle-windows.py', win_inst_opts)

  nsi_conf=configuration_data()
  nsi_conf.merge_from(conf)

  join_path_res=run_command(import('python').find_installation(),
	 '-c', 'from mesonbuild.scripts import destdir_join; from sys import argv; print(destdir_join(argv[1], argv[2]));',
	 meson.current_build_dir()/'package-ROOT', get_option('prefix'),
	 check: true)
  nsi_prefix=join_path_res.stdout().strip()

  nsi_conf.set('PACKAGE_TYPE', dist_type)
  nsi_conf.set('PACKAGE_PREFIX', '\\'.join(nsi_prefix.split('/')))
  nsi_conf.set('PACKAGE_SOURCE_DIR', '\\'.join(meson.current_source_dir().split('/')))
  nsi_file=configure_file(input: 'installer.nsi.in', output: 'installer.nsi',
    configuration: nsi_conf)

endif

if host_machine.system()=='darwin'

  reloc_intl=shared_library('intl-reloc', 'intl-reloc.c',
	 link_args: ['-Wl,-reexport-lintl'],
	 install: true, install_dir: get_option('libdir'))

  mac_exec=start_prog.name()
  if not inst_full
    mac_exec=proofread_prog.name()
  endif
  plist_conf=configuration_data()
  plist_conf.merge_from(conf)
  plist_conf.set('PACKAGE_EXEC', mac_exec)
  info_plist=configure_file(input: 'Info.plist.in', output: 'Info.plist',
    configuration: conf, install: true, install_dir: '/Contents')
  install_data('icon.icns', install_dir: '/Contents'/'Resources',
    rename: [meson.project_name()+'.icns'])
  configure_file(input: 'exec-wrapper.in', output: meson.project_name(),
    configuration: plist_conf, install: true,
    install_dir: '/Contents'/'MacOS', install_mode: 'rwxr-xr-x')
  meson.add_install_script('fix-bundle-macos.py', generic_inst_opts)
endif

