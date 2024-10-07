project('liblmdb', 'c')

w_flags='-W -Wall -Wno-unused-parameter -Wbad-function-cast -Wuninitialized'.split()
t_flags='-pthread'
o_flags='-O2 -g'.split()

liblmdb=library('lmdb',
  'libraries/liblmdb/mdb.c',
  'libraries/liblmdb/midl.c',
  link_args: '-pthread',
  c_args: [w_flags, t_flags, o_flags],
  install: true
  )

liblmdb_dep=declare_dependency(
  include_directories: 'libraries/liblmdb',
  link_with: liblmdb)

