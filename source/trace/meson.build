win32_srcs=[]
if host_machine.system()=='windows'
  win32_srcs+=import('windows').compile_resources(
	 corelib_ver_rc, corelib_dep_rc,
	 args: [],
	 depend_files: [corelib_ver_rc_deps, corelib_dep_rc_deps],
	 depends: [],
	 include_directories: [],
	 )
endif
trace_prog_deps=[
  corelib_dep,
  boost_dep,
  ]
if cpp.get_id()=='msvc'
  trace_prog_deps+=getopt_dep
endif

eval_srcs=[]
eval_deps=[corelib_dep]
if get_option('with-torch').enabled()
  torch_pfx=get_option('torch-prefix')
  torch_dep=declare_dependency(
	 compile_args: [
		'-D_GLIBCXX_USE_CXX11_ABI=1',
		'-isystem', torch_pfx/'include',
		'-isystem', torch_pfx/'include/torch/csrc/api/include',
		],
	 link_args: [
		'-Wl,-rpath,'+torch_pfx/'lib',
		'-Wl,-L,'+torch_pfx/'lib',
		'-lc10', '-lc10_cuda',
		'-Wl,--no-as-needed,-ltorch_cpu,-ltorch_cuda,--as-needed',
		],
	 dependencies: dependency('cuda', required: true)
  )
  eval_deps+=torch_dep
  eval_srcs+='evaluator-torch-resnet.cc'
  eval_srcs+='detector-torch-unet.cc'
  eval_srcs+=['torch-networks.cc']
else
  eval_srcs+='evaluator-dumb.cc'
endif
trace_eval=static_library('trace-evaluator',
  eval_srcs,
  dependencies: eval_deps,
  )

compute_src=['compute-common.cc']
if get_option('with-torch').enabled() and false
  trace_prog_deps+=torch_dep
  compute_src+=['compute-torch.cc']
elif host_machine.system()=='linux'
  neutube_dep=dependency('neutube', required: false)
  if not neutube_dep.found()
    neutube_proj=subproject('neutube', required: true)
    neutube_dep=neutube_proj.get_variable('libneurolabi_dep')
  endif
  trace_prog_deps+=neutube_dep
  compute_src+=['compute-neutube.cc']
  compute_src+=['neutube-helper.cc']
else
  compute_src+=['compute-dumb.cc']
endif

trace_prog=executable(proj_tarname+'-trace',
  compute_src,
  'evaluator-train.cc',
  'detector-train.cc',
  'main.cc',
  'reset-proofread.cc',
  'utils.cc',
  #eval_srcs,
  win32_srcs,
  win_subsystem: 'console',
  include_directories: [proj_inc],
  # XXX to use CUDA, libtorch_cuda MUST be loaded.
  #     no way to load at runtime, so force loading at compile time!
  # XXX also note that, meson cannot use generator exp in torchconfig.cmake,
  #     so modify that file.
  link_args: meson.get_compiler('cpp').get_linker_id()=='ld64'?[]:['-Wl,--no-as-needed'],
  dependencies: [
	 trace_prog_deps,
	 #	neurolabi::c
	 zlib_dep,
	 thr_dep,
	 dependency('gsl', required: true),
	 ],
  link_with: [
	 trace_eval,
	 ],
  install: inst_full)

