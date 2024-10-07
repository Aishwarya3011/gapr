project('lua', 'c')

prog_make=find_program('make', required: true)
build_cmd=[prog_make, '-j',
  'CC='+' '.join(meson.get_compiler('c').cmd_array()),
  '-C', meson.current_build_dir(),
  '-f', meson.current_source_dir()/'src'/'Makefile',
  'MYCFLAGS=-fPIC',
  'VPATH='+meson.current_source_dir()/'src'
]
liblua_a=custom_target(meson.project_name()+'-make',
  console: true,
  command: build_cmd+['liblua.a'],
  depend_files: [meson.current_source_dir()/'src'/'Makefile'],
  output: 'liblua.a',
)

lua_so=shared_library('lua',
  link_whole: [liblua_a],
  link_args: ['-lm'],
  name_prefix: 'lib',
  install: true,
)

lua_dep=declare_dependency(
  include_directories: include_directories('src'),
  link_with: lua_so,
)

