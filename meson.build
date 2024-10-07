project('Gapr', 'c', 'cpp',
  version: '1.1.0.11',
  license: 'GPLv3+',
  default_options: [
	 'warning_level=1',
	 'buildtype=debugoptimized',
	 'cpp_std=c++17',
	 ]
  )

# NOTE version updates:
# - android/proofread/build.gradle
# - android/proofread/src/main/cpp/CMakeLists.txt

proj_version=meson.project_version()
proj_ver_major=proj_version.split('.')[0].to_int()
proj_ver_minor=proj_version.split('.')[1].to_int()
proj_ver_micro=proj_version.split('.')[2].to_int()
proj_ver_patch=proj_version.split('.')[3].to_int()
proj_version='@0@.@1@.@2@'.format(proj_ver_major, proj_ver_minor, proj_ver_micro)

if not ['linux', 'windows', 'darwin'].contains(host_machine.system())
  #"${CMAKE_SYSTEM_NAME}" MATCHES "^Linux|Darwin|Windows$")
  error('System not supported: @0@.'.format(host_machine.system()))
endif
if not ['x86_64'].contains(host_machine.cpu())
  #"${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^x86_64|AMD64$")
  error('CPU not supported: @0@.'.format(host_machine.cpu()))
endif
#if(CMAKE_CROSSCOMPILING)
#	#message(FATAL_ERROR "Cross-compiling not supported.")
#endif

cpp=meson.get_compiler('cpp')
if not cpp.compiles(files('utilities/check-cpp17.cc'))
  error('C++17 must be available.')
endif
if not cpp.compiles('#include <filesystem>\nvoid f() { }')
  error('C++ <filesystem> is required.')
endif
if not cpp.links(files('utilities/check-charconv.cc'))
  error('C++ <charconv> is required.')
endif

proj_tarname=meson.project_name().to_lower()
conf=configuration_data()
conf.set('PACKAGE_NAME', meson.project_name())
conf.set('PACKAGE_TARNAME', proj_tarname)
conf.set('PACKAGE_VERSION', meson.project_version())
conf.set('PACKAGE_DESCRIPTION', 'Gapr accelerates projectome reconstruction')
conf.set('PACKAGE_URL', 'http://yanlab.org.cn/gapr/')
conf.set('PACKAGE_COPYRIGHT', 'Copyright (C) 2019-2021 GOU Lingfeng')
conf.set('PACKAGE_ORG', 'Yan Lab.')
conf.set('PACKAGE_DOMAIN', 'cn.org.yanlab')
conf.set('APPLICATION_ID', conf.get('PACKAGE_DOMAIN')+'.'+conf.get('PACKAGE_NAME'))
conf.set('APPLICATION_PATH', '/'+'/'.join(conf.get('APPLICATION_ID').split('.')))
conf.set('GETTEXT_PACKAGE', proj_tarname)
conf.set('PACKAGE_VERSION_MAJOR', proj_ver_major)
conf.set('PACKAGE_VERSION_MINOR', proj_ver_minor)
conf.set('PACKAGE_VERSION_MICRO', proj_ver_micro)
conf.set('PACKAGE_VERSION_PATCH', proj_ver_patch)
foreach dir: ['bin', 'data', 'include', 'info', 'lib', 'libexec', 'locale', 'localstate', 'man', 'sbin', 'sharedstate', 'sysconf']
  conf.set('PACKAGE_'+dir.to_upper()+'DIR', join_paths(get_option('prefix'), get_option(dir+'dir')))
endforeach

inst_full=true
inst_lite=true
if get_option('install-type')=='dist-lite'
  inst_full=false
endif

subdir('source')
subdir('po')
subdir('data')
subdir('doc/licenses')

if get_option('install-type')!='system'
  subdir('packaging')
endif
#subdir('doc')

setup_msg='''---------- Summary ------------
@0@ - @1@
 Build type ................ : @2@
 Boost.Asio debugging ...... : @3@
 VP9 Support ............... : @4@
 HEVC Support .............. : @5@
 Qt5 GUI ................... : @6@
  -- translations .......... : @7@
'''.format(meson.project_name(), meson.project_version(),
  get_option('buildtype'),
  get_option('debug-asio'),
  get_option('enable-vp9'),
  get_option('enable-hevc'),
  get_option('enable-gui'),
'xxx')
message(setup_msg)

