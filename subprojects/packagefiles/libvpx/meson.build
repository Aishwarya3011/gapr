project('libvpx', 'c')

prog_sh=find_program('bash', 'sh', required: true)
prog_yasm=find_program('yasm', 'nasm', required: true)

vpx_cfg_helper=[]
if meson.is_cross_build()
  #! Use CROSS env. to cross-compile!!!
  dump_res=run_command(meson.get_compiler('c'), '-dumpmachine', check: true)
  chost=dump_res.stdout().strip()
  vpx_cfg_helper+='export CROSS='+chost+'-'
  vpx_cfg_helper+='export CHOST='+chost
endif
vpx_cfg_helper+=['cd "$1"', 'shift', 'source "$0" "$@"']
vpx_cfg='--disable-install-bins --disable-install-libs --disable-examples --disable-tools --disable-docs --enable-vp9-highbitdepth --enable-vp9 --disable-unit-tests --disable-webm-io --disable-libyuv --disable-shared --enable-static --enable-pic'

makef=custom_target(meson.project_name()+'-configure',
  console: true,
  command: [prog_sh, '-c', '; '.join(vpx_cfg_helper),
	 meson.current_source_dir()/'configure', meson.current_build_dir(),
	 vpx_cfg.split()],
  input: 'configure',
  output: 'Makefile',
)

prog_make=find_program('make', required: true)
libvpx_a=custom_target(meson.project_name()+'-make',
  console: true,
  command: [prog_make, '-j', '-C', meson.current_build_dir()],
  depends: [makef],
  output: 'libvpx.a',
)

extra_deps=[]
if host_machine.system()=='linux'
  extra_deps+=dependency('threads', required: true)
  extra_deps+=meson.get_compiler('c').find_library('m')
endif

libvpx=shared_library('vpx',
  link_whole: libvpx_a,
  dependencies: extra_deps,
  name_prefix: 'lib',
  install: true,
)

libvpx_dep=declare_dependency(
  include_directories: include_directories('.'),
  link_with: libvpx,
)

