project('neutube', 'c')

prog_cmake=find_program('cmake', required: true)

skip_ccache=meson.get_compiler('cpp').cmd_array()[0]=='ccache' ? 1 : 0
cmake_cache=custom_target(meson.project_name()+'configure',
  console: true,
  command: [prog_cmake, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-fPIC', '-DUSE_LIBFFTW=ON', '-DUSE_LIBFFTWF=ON', '-DUSE_LIBJANSSON=OFF', '-DUSE_LIBXML2=OFF', '-S', meson.current_source_dir()/'neurolabi'/'c', '-B', meson.current_build_dir(), '-DCMAKE_CXX_COMPILER='+(' '.join([meson.get_compiler('cpp').cmd_array()[skip_ccache]]))],
  input: meson.current_source_dir()/'neurolabi'/'c'/'CMakeLists.txt',
  output: 'CMakeCache.txt',
)

prog_ninja=find_program('ninja', required: true)

libneurolabi_a=custom_target(meson.project_name()+'-make',
  console: true,
  command: [prog_ninja, '-C', meson.current_build_dir(), 'cmake_object_order_depends_target_neurolabi', 'libneurolabi.a'],
  depends: [cmake_cache],
  output: 'libneurolabi.a',
)

libs=['-lz']
extra_deps=[]
#if host_machine.system()=='windows'
  #libs+=
  #endif
extra_deps+=meson.get_compiler('c').find_library('m')
fftw=dependency('fftw3', required: true)
fftwf=dependency('fftw3f', required: true)
extra_deps+=[fftw, fftwf]

libneurolabi=shared_library('neurolabi',
  link_whole: libneurolabi_a,
  link_args: [libs],
  dependencies: extra_deps,
  name_prefix: 'lib',
  install: true,
)

libneurolabi_dep=declare_dependency(
  include_directories: [
    include_directories('neurolabi'/'c'),
    include_directories('neurolabi'/'c'/'include'),
    include_directories('neurolabi'/'lib'/'genelib'/'src'),
    ],
  link_with: libneurolabi,
)

