others=[
  'LICENSE.adwaita',
  'LICENSE.boost',
  'LICENSE.curl',
  'LICENSE.gplv3',
  'LICENSE.hm',
  'LICENSE.lgplv2',
  'LICENSE.lgplv2.1',
  'LICENSE.lgplv3',
  'LICENSE.libtiff',
  'LICENSE.libwebm',
  'LICENSE.mit',
  'LICENSE.neutube',
  'LICENSE.neutube2',
  'LICENSE.ofl',
  'LICENSE.oldap2.8',
  'LICENSE.openssl',
  'LICENSE.vpx',
  'LICENSE.zlib',
]

install_data('COPYING.adoc',
  install_dir: get_option('datadir') /'licenses'/proj_tarname,
  install_mode: 'rw-r--r--')

foreach f: others
  install_data(f,
    install_dir: get_option('datadir') /'licenses'/proj_tarname,
    install_mode: 'rw-r--r--')
endforeach

