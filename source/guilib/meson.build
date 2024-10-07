qt5=import('qt5')

guilib_name=meson.project_name()+'Gui'
guilib_conf=configuration_data()
guilib_conf.merge_from(corelib_conf)
guilib_conf.set('GUILIB_NAME', guilib_name)
guilib_conf.set('GUILIB_VER', proj_version)

guilib_mocs=qt5.preprocess(
  moc_headers: [
	 'about-dialog.hh',
	 'gapr/gui/application.hh',
	 'gapr/gui/dialogs.hh',
	 'gapr/gui/login-dialog.hh',
	 'gapr/gui/range-widget.hh',
	 ],
  moc_sources: [
	 'login-dialog.cc',
	 ],
  ui_files: [
	 'ui/dlg-about.ui',
	 'ui/dlg-login.ui',
	 'ui/dlg-secret.ui',
	 'ui/wid-range-slave.ui',
	 'ui/wid-range.ui',
	 ],
  qresources: ['../../data/icons/icons.qrc'],
  include_directories: [],
  dependencies: [],
  )
subdir('translations')

guilib_srcs=files(
  'about-dialog.cc',
  'application.cc',
  'dialogs.cc',
  'login-dialog.cc',
  'program.cc',
  'range-widget.cc',
  'utility.cc',
  )

if host_machine.system()=='windows'
  manif_rc=configure_file(input: 'manifest.rc.in', output: 'manifest.rc', copy: true)
  manif_file=configure_file(input: 'manifest.in', output: 'manifest', configuration: guilib_conf)
  guilib_srcs+=import('windows').compile_resources(manif_rc,
	 args: [],
	 depend_files: [manif_file],
	 depends: [],
	 include_directories: [],
	 )
endif

guilib_inc=include_directories('.')

guilib=shared_library(guilib_name,
  guilib_srcs, guilib_mocs,
  soversion: proj_version,
  version: proj_version,
  name_prefix: 'lib',
  darwin_versions: proj_version,
  include_directories: [proj_inc],
  cpp_args: ['-DGAPR_GUI_COMPILATION'],
  link_args: [],
  link_with: [],
  dependencies: [
	 corelib_dep,
	 thr_dep,
	 qt5_dep,
	 ],
  install: inst_lite)

guilib_dep=declare_dependency(
  compile_args: [],
  dependencies: [],
  include_directories: [guilib_inc],
  link_args: [],
  link_with: guilib,
  link_whole: [],
  sources: [])

guilib_icon_rc=files('icon.rc')
guilib_icon_rc_deps=files('icon.ico')

guilib_dep_rc=configure_file(input: 'manifest.dep.rc.in', output: 'manifest.dep.rc', copy: true)
guilib_dep_rc_deps=configure_file(input: 'manifest.dep.in', output: 'manifest.dep', configuration: guilib_conf)
