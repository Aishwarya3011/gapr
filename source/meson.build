cpp_flags=[]
link_flags=[]
if cpp.get_id()=='msvc'
  cpp_flags+=['-D_WIN32_WINNT=0x0601']
endif

if cpp.get_linker_id()=='ld64'
  link_flags+=['-Wl,-undefined', '-Wl,error']
elif cpp.get_linker_id()=='link'
else
  link_flags+=['-Wl,--no-undefined']
endif

boost_mods=['context', 'system', 'iostreams']

omp_dep=dependency('openmp', required: false)
thr_dep=dependency('threads', required: true)
zlib_dep=dependency('zlib', required: true)
lmdb_dep=dependency('lmdb', required: true, fallback: ['liblmdb', 'liblmdb_dep'])
tiff_dep=dependency('tiff', required: false)
if not tiff_dep.found()
  tiff_dep=dependency('libtiff-4', required: true)
endif
ssl_dep=dependency('openssl', required: true)
curl_dep=dependency('CURL', required: false, modules: ['CURL::libcurl'])
if not curl_dep.found()
  curl_dep=dependency('libcurl', required: true)
endif

dl_dep=[]
if cpp.get_id()!='msvc'
  dl_dep=cpp.find_library('dl', required: true, has_headers: ['dlfcn.h'])
endif

getopt_dep=[]
if cpp.get_id()=='msvc'
  getopt_dep=cpp.find_library('getopt', required: true, has_headers: ['getopt.h'])
endif

if get_option('with-system-boost')
  boost_dep=dependency('boost', required: true, version: '>=1.70.0',
	 modules: boost_mods)
  boost_proc_dep=dependency('boost', required: true, version: '>=1.70.0',
    modules: ['filesystem'])
else
  boost_proj=subproject('boost', required: true, default_options: [])
  boost_dep=boost_proj.get_variable('boost_dep')
  boost_proc_dep=boost_dep
endif
cpp_flags+=['-DBOOST_ASIO_DYN_LINK', '-DBOOST_ASIO_NO_DEPRECATED']
cpp_flags+='-DBOOST_ASIO_NO_TS_EXECUTORS'
if get_option('debug-asio')
  cpp_flags+=['-DBOOST_ASIO_ENABLE_HANDLER_TRACKING']
endif

if get_option('enable-vp9').enabled()
  vpx_dep=[]
  if get_option('with-system-libvpx')
    vpx_dep+=dependency('vpx', required: true)
  else
    vpx_proj=subproject('libvpx', required: true, default_options: [])
    vpx_dep+=vpx_proj.get_variable('libvpx_dep')
  endif
  if get_option('with-system-libwebm')
    vpx_dep+=dependency('webm', required: true)
  else
    webm_proj=subproject('libwebm', required: true, default_options: [])
    vpx_dep+=webm_proj.get_variable('libwebm_dep')
  endif
endif
if get_option('enable-hevc').enabled()
  hevc_sys_dep=dependency('jctvc-hm', required: false)
  if hevc_sys_dep.found()
    hevc_dep=hevc_sys_dep
  else
  hevc_proj=subproject('jctvc-hm', required: true, default_options: [])
  hevc_dep=hevc_proj.get_variable('libhevc_dep')
  endif
endif
if get_option('enable-av1').enabled()
  aom_dep=dependency('aom', required: true)
endif

qt5_dep=dependency('Qt5', required: get_option('enable-gui'), version: '>=5.4',
  modules: ['Core', 'Gui', 'Widgets'])
if qt5_dep.found()
#OPTIONAL_COMPONENTS LinguistTools)
	#AC_CHECK_TOOL([LRELEASE], [lrelease-qt5], [lrelease])
  cpp_flags+=['-DQT_DEPRECATED_WARNINGS']
endif

add_project_arguments(cpp_flags, language: 'cpp')
add_project_link_arguments(link_flags, language: 'cpp')

conf_hh=configure_file(input: 'config.hh.in', output: 'config.hh', configuration: conf)
proj_inc=include_directories('.')
configure_file(input: 'source-dir.in', output: 'source-dir', configuration: {'SOURCE_DIR': meson.current_source_dir()})

subdir('corelib')
if qt5_dep.found()
  subdir('guilib')
endif

#add_executable(cli-prog "main.cc")
#set_target_properties(cli-prog PROPERTIES
#RUNTIME_OUTPUT_NAME "gapr")
#install(TARGETS cli-prog
#RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
#COMPONENT core
#)
#target_link_libraries(cli-prog PRIVATE core-lib)

subdir('gather')
subdir('export')
subdir('trace')
subdir('brief')
subdir('map')
subdir('convert')

if qt5_dep.found()
  subdir('start')
  subdir('fix')
  subdir('proofread')
  subdir('show')
endif

