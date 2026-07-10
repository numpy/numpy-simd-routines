# numpy_sr package: Python harness + pytest suite + per-op kernel .so, all
# installed under one importable numpy_sr/.

# Op sources and pytest files live together in npsr/**/tests/. Globs aren't
# tracked by meson: rerun `spin build` after adding one.
_scan = run_command(
  py,
  '-c',
  'import sys, pathlib; print("\\n".join(sorted(str(p) for p in pathlib.Path(sys.argv[1]).glob("**/tests/*.cc"))))',
  meson.project_source_root() / 'npsr',
  check: true,
).stdout().strip()

npsr_test_ops = _scan == '' ? [] : _scan.split('\n')

_scan_py = run_command(
  py,
  '-c',
  'import sys, pathlib; print("\\n".join(sorted(str(p) for p in pathlib.Path(sys.argv[1]).glob("**/tests/test_*.py"))))',
  meson.project_source_root() / 'npsr',
  check: true,
).stdout().strip()

npsr_test_files = _scan_py == '' ? [] : _scan_py.split('\n')

py.install_sources(
  ['__init__.py', 'conftest.py', 'testing.py'],
  subdir: 'numpy_sr',
)
py.install_sources(
  ['tests/__init__.py'] + npsr_test_files,
  subdir: 'numpy_sr/tests',
)

package_install_dir = py.get_install_dir() / 'numpy_sr'
extensions_install_dir = package_install_dir / 'extensions'

hwy_proj = subproject(
  'highway',
  default_options: [
    'contrib=disabled',
    'examples=disabled',
    # 'tests' alone still pulls googletest; 'test_standalone' cuts it too.
    'tests=disabled',
    'test_standalone=true',
  ],
)
hwy_dep = hwy_proj.get_variable('hwy_dep')

# MPFR reference oracle (pyext-mpfr.h). Prefer pkg-config: its libdir feeds
# install_rpath below -- meson strips the build RUNPATH at install, which
# orphans libmpfr.so.6 on distros where it is outside the default search
# path (e.g. NixOS). Fall back to plain library + header lookup.
cpp = meson.get_compiler('cpp')
mpfr_dep = dependency('mpfr', required: false)
mpfr_rpath = ''
if mpfr_dep.found()
  mpfr_rpath = mpfr_dep.get_variable(pkgconfig: 'libdir', default_value: '')
else
  mpfr_dep = cpp.find_library('mpfr', has_headers: ['mpfr.h'])
endif

fs = import('fs')

# numpy_sr._numpy_sr: target detection (the only Highway-runtime link site)
# + FP exception flag readout for the test suite.
py.extension_module(
  '_numpy_sr',
  '_numpy_sr.cc',
  include_directories: npsr_inc,
  dependencies: hwy_dep,
  subdir: 'numpy_sr',
  install: true,
)

# One .so per op; pyext.cc gives each its own registry + npsr_py_load export.
# Only the MPFR oracle plugin (tests/mpfr.cc) links mpfr; ops link Highway.
foreach src : npsr_test_ops
  is_mpfr = fs.stem(src) == 'mpfr'
  shared_library(
    fs.stem(src),
    [src, 'pyext.cc'],
    include_directories: npsr_inc,
    dependencies: is_mpfr ? [mpfr_dep] : [hwy_dep],
    install_rpath: is_mpfr ? mpfr_rpath : '',
    install: true,
    install_dir: extensions_install_dir,
  )
endforeach
