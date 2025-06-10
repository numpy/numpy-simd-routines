project(
  'NumPy SIMD routines tests',
  'cpp', 
  version: '0.01',
  license: 'BSD-3',
  meson_version: '>=1.5.2', 
  default_options: [
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
    'cpp_std=c++17',
  ],
)

py = import('python').find_installation(pure: false)
npsr_dir = py.get_install_dir() / 'numpy_sr'

subdir('test')
subdir('npsr')
