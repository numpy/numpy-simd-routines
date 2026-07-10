project(
  'npsr',
  'cpp',
  version: '0.1.0',
  meson_version: '>= 1.3.0',
  license: 'BSD-3-Clause',
  default_options: [
    'cpp_std=c++17',
    'buildtype=release',
  ],
)

py = import('python').find_installation()
npsr_inc = include_directories('.')

# `numpy_sr` scans the op sources (npsr/**/tests/<op>.cc), builds them, and
# installs the Python package (harness + kernels + tests).
if get_option('tests').disable_auto_if(meson.is_subproject()).allowed()
  subdir('numpy_sr')
endif
