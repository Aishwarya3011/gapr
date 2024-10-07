project('boost', 'cpp', version: '1.72.0')
####################################

#./bin.v2/libs/context/build/clang-linux-x86/debug/target-os-android/threading-multi/visibility-hidden/cxx11_hdr_mutex_check.o

boost_context_srcs=files(
  'libs/context/src/asm/ontop_x86_64_sysv_elf_gas.S',
  'libs/context/src/asm/jump_x86_64_sysv_elf_gas.S',
  'libs/context/src/asm/make_x86_64_sysv_elf_gas.S',
  'libs/context/src/posix/stack_traits.cpp',
  )
if host_machine.system()=='windows'
  boost_context_srcs=files(
	 'libs/context/src/asm/jump_x86_64_ms_pe_masm.asm',
	 'libs/context/src/asm/make_x86_64_ms_pe_masm.asm',
	 'libs/context/src/asm/ontop_x86_64_ms_pe_masm.asm',
	 'libs/context/src/windows/stack_traits.cpp',
	 )
endif

boost_fs_srcs=files(
	'libs/filesystem/src/operations.cpp',
	'libs/filesystem/src/exception.cpp',
	'libs/filesystem/src/path.cpp',
	'libs/filesystem/src/directory.cpp',
	)

boost_context=library('boost-context',
  boost_context_srcs,
  boost_fs_srcs,

  #link_args: '-pthread',
  #c_args: [w_flags, t_flags, o_flags],
  install: true
  )

zlib_dep=dependency('zlib', required: true)
boost_ios=library('boost-iostreams',
  'libs/iostreams/src/zlib.cpp',
  'libs/iostreams/src/gzip.cpp',
  dependencies: [zlib_dep],
  install: true)

boost_dep=declare_dependency(
  include_directories: '.',
  link_with: [boost_context, boost_ios])

