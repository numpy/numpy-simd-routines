// numpy_sr._numpy_sr: Highway target detection (the only libhwy-runtime link
// site) + FP exception flags of the calling thread (where ctypes kernels run).
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cfenv>

// foreach_target probe: per-target registrars record compile-time
// HWY_NATIVE_FMA / HWY_HAVE_FLOAT64 into the registry (hwy_target-inl.h).
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "numpy_sr/hwy_target-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "numpy_sr/hwy_target-inl.h"

// Missing FE_* macros (e.g. Emscripten) degrade to 0, same as npsr/precise.h.
#ifndef FE_INVALID
#define FE_INVALID 0
#endif
#ifndef FE_DIVBYZERO
#define FE_DIVBYZERO 0
#endif
#ifndef FE_OVERFLOW
#define FE_OVERFLOW 0
#endif
#ifndef FE_UNDERFLOW
#define FE_UNDERFLOW 0
#endif
#ifndef FE_INEXACT
#define FE_INEXACT 0
#endif

namespace {

// targets() -> {HWY_* target bit: {"name", "has_fma", "has_float64"}} for
// every target this CPU can run (detected in hwy_target-inl.h).
PyObject* Targets(PyObject* /*self*/, PyObject* /*ignored*/) {
  PyObject* dict = PyDict_New();
  if (dict == nullptr) return nullptr;

  for (const npsr::py::TargetInfo& t : npsr::py::TargetRegistry()) {
    PyObject* key = PyLong_FromLongLong(t.bit);
    // "{sssNsN}": copy the strings, steal the two fresh bool references.
    PyObject* value = Py_BuildValue(
        "{sssNsN}", "name", t.name, "has_fma", PyBool_FromLong(t.has_fma),
        "has_float64", PyBool_FromLong(t.has_float64));
    if (key == nullptr || value == nullptr ||
        PyDict_SetItem(dict, key, value) < 0) {
      Py_XDECREF(key);
      Py_XDECREF(value);
      Py_DECREF(dict);
      return nullptr;
    }
    Py_DECREF(key);
    Py_DECREF(value);
  }
  return dict;
}

PyObject* FenvClear(PyObject* /*self*/, PyObject* /*ignored*/) {
  std::feclearexcept(FE_ALL_EXCEPT);
  Py_RETURN_NONE;
}

PyObject* FenvTest(PyObject* /*self*/, PyObject* /*ignored*/) {
  return PyLong_FromLong(std::fetestexcept(FE_ALL_EXCEPT));
}

PyMethodDef kMethods[] = {
    {"targets", Targets, METH_NOARGS,
     "targets() -> dict[int, dict]\n\n"
     "Map of {HWY_* target bit: {'name', 'has_fma', 'has_float64'}} for every "
     "SIMD target this CPU supports."},
    {"fenv_clear", FenvClear, METH_NOARGS,
     "fenv_clear()\n\nClear all floating-point exception flags."},
    {"fenv_test", FenvTest, METH_NOARGS,
     "fenv_test() -> int\n\nCurrently raised FP exception flags (FE_* bits)."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef kModule = {
    PyModuleDef_HEAD_INIT, "_numpy_sr",
    "C support for numpy_sr: SIMD target detection + FP exception flags.",
    -1, kMethods, nullptr, nullptr, nullptr, nullptr,
};

}  // namespace

PyMODINIT_FUNC PyInit__numpy_sr(void) {
  PyObject* mod = PyModule_Create(&kModule);
  if (mod == nullptr) return nullptr;
  if (PyModule_AddIntConstant(mod, "FE_INVALID", FE_INVALID) < 0 ||
      PyModule_AddIntConstant(mod, "FE_DIVBYZERO", FE_DIVBYZERO) < 0 ||
      PyModule_AddIntConstant(mod, "FE_OVERFLOW", FE_OVERFLOW) < 0 ||
      PyModule_AddIntConstant(mod, "FE_UNDERFLOW", FE_UNDERFLOW) < 0 ||
      PyModule_AddIntConstant(mod, "FE_INEXACT", FE_INEXACT) < 0 ||
      PyModule_AddIntConstant(
          mod, "FE_ERRORS",
          FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW) < 0) {
    Py_DECREF(mod);
    return nullptr;
  }
  return mod;
}
