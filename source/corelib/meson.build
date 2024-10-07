subdir('gapr')

corelib_name=meson.project_name()+'Core'
corelib_conf=configuration_data()
corelib_conf.set('CORELIB_NAME', corelib_name)
corelib_conf.set('CORELIB_VER', proj_version)
corelib_conf.set('CORELIB_ASIO_VER', boost_dep.version())

#nghttp2_dep=dependency('libnghttp2')

corelib_srcs=files(
  'acceptor.cc',
  'archive.cc',
  'buffer.cc',
  'connection.cc',
  #'conn-h2.cc',
  'cube.cc',
  'cube-loader.cc',
  'cube-builder.cc',
  'downloader.cc',
  'fiber.cc',
  'filter.cc',
  'mem-file-cache.cc',
  'model.cc',
  'edge-model.cc',
  'options.cc',
  'parser.cc',
  'plugin-loader.cc',
  'program.cc',
  'promise.cc',
  'streambuf.cc',
  'trace-api.cc',
  'utility.cc',
  'swc-helper.cc',
  'detail/asio-misc.cc',
  'detail/cube-loader-nrrd.cc',
  'detail/cube-loader-tiff.cc',
  'detail/cube-loader-v3d.cc',
  )

corelib_inc=include_directories('.')

corelib_w32_deps=[]
corelib_asio_w32_deps=[]
if host_machine.system()=='windows'
  corelib_w32_deps+=cpp.find_library('ws2_32')
  corelib_w32_deps+=cpp.find_library('userenv')
  corelib_asio_w32_deps+=cpp.find_library('mswsock')
  corelib_asio_w32_deps+=cpp.find_library('bcrypt')
  manif_rc=configure_file(input: 'manifest.rc.in', output: 'manifest.rc', copy: true)
  manif_file=configure_file(input: 'manifest.in', output: 'manifest', configuration: corelib_conf)
  corelib_srcs+=import('windows').compile_resources(manif_rc,
	 depend_files: [manif_file])
endif


extra_args=[]
extra_deps=[]
if get_option('enable-vp9').enabled()
  corelib_srcs+=files('detail/cube-loader-vp9.cc')
  extra_args+='-DWITH_VP9'
  extra_deps+=vpx_dep
endif
if get_option('enable-av1').enabled()
  corelib_srcs+=files('detail/cube-loader-vp9.cc')
  extra_args+='-DWITH_AV1'
  extra_deps+=aom_dep
endif
if get_option('enable-hevc').enabled()
  corelib_srcs+=files('detail/cube-loader-hevc.cc')
  extra_args+='-DWITH_HEVC'
  extra_deps+=hevc_dep
endif

corelib_asio_extra_deps=[]
if host_machine.system()=='linux'
  corelib_asio_extra_deps+=thr_dep
endif

fix_asio_args=[]
if host_machine.system()=='windows'
  fix_asio_args+=['-include', 'gapr/detail/fix-boost-asio.hh']
endif
corelib_asio=shared_library(corelib_name+'-boost-asio', 'asio-src.cc',
  soversion: boost_dep.version(),
  version: boost_dep.version(),
  darwin_versions: boost_dep.version(),
  name_prefix: 'lib',
  include_directories: [proj_inc],
  cpp_args: [],
  #link_args: [],
  dependencies: [boost_dep, ssl_dep,
	 corelib_w32_deps,
	 corelib_asio_w32_deps,
	 corelib_asio_extra_deps,
	 ],
  install: inst_lite)

corelib=shared_library(corelib_name, corelib_srcs,
  soversion: proj_version,
  version: proj_version,
  darwin_versions: proj_version,
  name_prefix: 'lib',
  include_directories: [proj_inc],
  cpp_args: ['-DGAPR_CORE_COMPILATION', extra_args, fix_asio_args],
  link_args: [],
  dependencies: [
	 dl_dep,
	 thr_dep,
	 zlib_dep,
	 tiff_dep,
	 boost_dep,
	 ssl_dep,
	 curl_dep,
	 lmdb_dep,
	 corelib_w32_deps,
	 extra_deps,
	 ],
  link_with: [corelib_asio],
  install: inst_lite)

#
corelib_ver_rc=configure_file(input: 'version.rc.in', output: 'version.rc',
  configuration: conf)
corelib_ver_rc_deps=[]
corelib_dep_rc=configure_file(input: 'manifest.dep.rc.in', output: 'manifest.dep.rc', copy: true)
corelib_dep_rc_deps=configure_file(input: 'manifest.dep.in', output: 'manifest.dep', configuration: corelib_conf)
if host_machine.system()=='windows'
  
  corelib_res=[]
endif
#if(WIN32)
#	
#
#else()
#	target_link_libraries(core-lib PRIVATE "${CMAKE_DL_LIBS}")
#endif()
#if(ANDROID)
#	target_link_libraries(core-lib PRIVATE "-llog")
#endif()
#
#if(OpenMP_FOUND)
#	if(ENABLE_VP9 OR ENABLE_HEVC)
#		target_compile_definitions(core-lib PRIVATE HAVE_OPENMP)
#		target_link_libraries(core-lib PRIVATE OpenMP::OpenMP_CXX)
#	endif()
#endif()
#if(NOT HAVE_STD_FILESYSTEM)
#	target_compile_definitions(core-lib PRIVATE STD_FILESYSTEM_MISSING)
#	target_link_libraries(core-lib PRIVATE Boost::filesystem)
#endif()
#if(NOT HAVE_STD_CHARCONV)
#	target_compile_definitions(core-lib PRIVATE STD_CHARCONV_MISSING)
#endif()

## XXX install dirs/licenses...

corelib_dep=declare_dependency(
  compile_args: fix_asio_args,
  dependencies: [
    ssl_dep.partial_dependency(compile_args: true),
    boost_dep.partial_dependency(compile_args: true, includes: true),
  ],
  include_directories: [corelib_inc],
  link_args: [],
  link_with: [corelib, corelib_asio],
  link_whole: [],
  sources: [])

