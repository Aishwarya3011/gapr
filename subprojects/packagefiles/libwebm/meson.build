project('libwebm', 'cpp')

libwebm=shared_library('webm',
  'mkvmuxer.cpp',
  'mkvmuxer.hpp',
  'mkvmuxertypes.hpp',
  'mkvmuxerutil.cpp',
  'mkvmuxerutil.hpp',
  'mkvparser.cpp',
  'mkvparser.hpp',
  'mkvreader.cpp',
  'mkvreader.hpp',
  'mkvwriter.cpp',
  'mkvwriter.hpp',
  'webmids.hpp',
  name_prefix: 'lib',
  install: true,
)

libwebm_dep=declare_dependency(
  include_directories: include_directories('.'),
  link_with: libwebm,
  )

