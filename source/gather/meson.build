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
gather_prog_deps=[
  corelib_dep,
  thr_dep,
  boost_dep,
  ssl_dep,
  ]
if cpp.get_id()=='msvc'
  gather_prog_deps+=getopt_dep
endif

gather_prog=executable(proj_tarname+'-gather',
  'env.cc',
  'main.cc',
  'model.cc',
  'server.cc',
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  cpp_args: ['-DBOOST_BEAST_USE_STD_STRING_VIEW'],
  link_args: [],
  dependencies: [gather_prog_deps],
  install: inst_full)

if inst_full
  install_data(
	 'docroot/admin.html',
	 'docroot/index.html',
	 'docroot/locked.html',
	 'docroot/login.html',
	 'docroot/progress.html',
	 'docroot/rank.html',
	 'docroot/register.html',
	 install_dir: get_option('datadir')/proj_tarname/'docroot',
	 install_mode: 'r--r--r--')
  install_data(
	 'docroot/css/bootstrap.min.css',
	 'docroot/css/fontawesome-all.min.css',
	 install_dir: get_option('datadir')/proj_tarname/'docroot'/'css',
	 install_mode: 'r--r--r--')
  install_data(
	 'docroot/js/jquery-3.2.1.slim.min.js',
	 'docroot/js/bootstrap.min.js',
	 'docroot/js/popper.min.js',
	 install_dir: get_option('datadir')/proj_tarname/'docroot'/'js',
	 install_mode: 'r--r--r--')
  install_data(
	 'docroot/webfonts/fa-solid-900.woff2',
	 install_dir: get_option('datadir')/proj_tarname/'docroot'/'webfonts',
	 install_mode: 'r--r--r--')
endif

upgrade_repo_prog=executable(proj_tarname+'-upgrade-repo',
  'helpers/upgrade-repo.cc',
  dependencies: [corelib_dep],
  install: inst_full)

#if(NOT HAVE_STD_FILESYSTEM)
	#target_compile_definitions(gather PRIVATE STD_FILESYSTEM_MISSING)
	#target_link_libraries(gather PRIVATE Boost::filesystem)
#endif()

