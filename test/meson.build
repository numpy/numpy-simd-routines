highway_lib = static_library('highway',
  [
    'highway/hwy/abort.cc',
    'highway/hwy/aligned_allocator.cc',
    'highway/hwy/nanobenchmark.cc',
    'highway/hwy/per_target.cc',
    'highway/hwy/perf_counters.cc',
    'highway/hwy/print.cc',
    'highway/hwy/targets.cc',
    'highway/hwy/timer.cc',
  ],
  cpp_args: '-DTOOLCHAIN_MISS_ASM_HWCAP_H',
  include_directories: ['highway'],
  install: false,
  gnu_symbol_visibility: 'hidden',
)

py.extension_module('_intrins',
  ['src/module.cpp'],
  link_with: [
    highway_lib,
  ],
  include_directories: ['highway', '../'],
  install: true,  
  install_dir: npsr_dir,
)
py.install_sources(
  ['__init__.py', 'datatypes.py', 'conftest.py', 'utils.py'],
  subdir: npsr_dir,
)

install_subdir('test', install_dir: npsr_dir/'test')
