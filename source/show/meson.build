show_mod_sources = [
  'main.cc',
  'window.cc',
  '../gather/model.cc',
]

lua_dep=dependency('lua', required: false)
if not lua_dep.found()
    lua_proj=subproject('lua', required: true, default_options: [])
    lua_dep=lua_proj.get_variable('lua_dep')
endif

show_mod_deps = [
  boost_dep,
  lua_dep,
  ]

if cpp.get_id()=='msvc'
  show_mod_deps+=[
	 cpp.find_library('gtk-3.0'),
	 cpp.find_library('epoxy'),
	 cpp.find_library('glib-2.0'),
	 cpp.find_library('gobject-2.0'),
	 cpp.find_library('gdk-3.0'),
	 cpp.find_library('gio-2.0'),
	 cpp.find_library('libintl'),
	 cpp.find_library('gtk-3.0'),
	 cpp.find_library('epoxy'),
]
else
  show_mod_deps+=[
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk+-3.0', version: '>= 3.22'),
  dependency('epoxy')
]
endif

gnome = import('gnome')

show_mod_sources += gnome.compile_resources('show-resources',
  'gresource.xml', c_name: 'show'
)

win32_srcs=[]
if host_machine.system()=='windows'
  win32_srcs+=import('windows').compile_resources(
	 corelib_ver_rc,
	 corelib_dep_rc,
	 guilib_icon_rc,
	 args: [],
	 depend_files: [corelib_ver_rc_deps, corelib_dep_rc_deps, guilib_icon_rc_deps],
	 depends: [],
	 include_directories: [],
	 )
endif

show_prog=executable(proj_tarname+'-show', show_mod_sources,
  win32_srcs,
  win_subsystem: 'windows',
  dependencies: [show_mod_deps, corelib_dep, getopt_dep],
  link_with: [],
  link_args: [],
  include_directories: [proj_inc, guilib_inc],
  install: inst_full,
)
