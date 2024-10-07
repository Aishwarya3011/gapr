desktop_in=configure_file(input: 'app.desktop.in.in',
  output: '@BASENAME@',
  configuration: conf,
  )
import('i18n').merge_file(input: desktop_in,
  output: conf.get('APPLICATION_ID')+'.desktop',
  type: 'desktop', po_dir: '../po',
  install: inst_full,
  install_dir: get_option('datadir')/'applications')

appdata_in=configure_file(input: 'app.appdata.xml.in',
  output: '@BASENAME@',
  configuration: conf,
  )
import('i18n').merge_file(input: appdata_in,
  output: conf.get('APPLICATION_ID')+'.appdata.xml',
  po_dir: '../po',
  install: inst_full,
  install_dir: get_option('datadir')/'metainfo')

icons=[
  ['actions', [
    'goto-target.svg',
    'tracing-end.svg',
    'goto-position.svg',
    'tracing-extend.svg',
    'tracing-branch.svg',
    'tracing-connect.svg',
    'pick-current.svg',
    ], ],
  ]
foreach iconset: icons
  foreach siz: ['32x32']
    foreach icon: iconset[1]
      install_data('icons'/siz/iconset[0]/icon,
        install_dir: get_option('datadir')/'icons'/'hicolor'/siz/iconset[0],
        install_mode: 'rw-r--r--')
    endforeach
  endforeach
endforeach

if inst_full
  foreach siz: ['64x64', '32x32', '16x16', 'scalable']
    install_data('icons'/siz/'apps/app-icon.svg',
      rename: conf.get('APPLICATION_ID')+'.svg',
      install_dir: get_option('datadir')/'icons'/'hicolor'/siz/'apps',
      install_mode: 'rw-r--r--')
  endforeach
endif

