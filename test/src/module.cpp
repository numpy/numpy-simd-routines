#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/module.cpp"

#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include <string>

#include "bind-inl.h"
#include "helper-intrins-inl.h"
#include "prec-bind-inl.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE {
using namespace test;
namespace hn = hwy::HWY_NAMESPACE;

template <int WIDTH, typename TLane>
struct TagByWidthHelper {
  using type = hn::FixedTag<TLane, WIDTH>;  // Only instantiated when WIDTH != 0
};

template <typename TLane>
struct TagByWidthHelper<0, TLane> {
  using type = hn::ScalableTag<TLane>;  // Only instantiated when WIDTH == 0
};

template <int WIDTH, typename TLane>
using TagByWidth = typename TagByWidthHelper<WIDTH, TLane>::type;
template <int WIDTH, typename... TLane>
using VecsByWidth = std::tuple<hn::VFromD<TagByWidth<WIDTH, TLane>>...>;

template <typename PreciseTuple, typename VecTuple>
HWY_ATTR bool LoadPreciseIntrinsics(PyObject *m, PreciseTuple, VecTuple);

template <typename... TPrecise, typename... TVec>
HWY_ATTR bool LoadPreciseIntrinsics(PyObject *m, std::tuple<TPrecise...>,
                                    std::tuple<TVec...>) {
  bool r = true;
  ((r &= PreciseBind<TPrecise, TVec...>(m)), ...);
  return r;
}

template <typename... TVec>
HWY_ATTR bool LoadIntrinsics(PyObject *m, std::tuple<TVec...>) {
  bool r = true;
  ((r &= Bind<TVec>(m)), ...);
  return r;
}

template <typename... TVec>
HWY_ATTR bool LoadHelpersIntrinsics(PyObject *m, std::tuple<TVec...>) {
  bool r = true;
  ((r &= BindHelpers<TVec>(m)), ...);
  return r;
}

template <int WIDTH = 0>
HWY_ATTR PyObject *Target(bool &escape) {
#if HWY_NATIVE_FMA
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
  static std::string module_name = "npsr." TOSTRING(HWY_NAMESPACE) + []() {
    if constexpr (WIDTH == 0)
      return std::string("");
    else
      return "_force" + std::to_string(WIDTH);
  }();

  static PyModuleDef defs = {
      PyModuleDef_HEAD_INIT, module_name.c_str(), "", -1, nullptr,
  };

  PyObject *m = PyModule_Create(&defs);
  if (m == nullptr) {
    return nullptr;
  }
  constexpr std::pair<const char *, int> constants[] = {
      {"_HAVE_FLOAT16", HWY_HAVE_FLOAT16},
      {"_HAVE_FLOAT64", HWY_HAVE_FLOAT64},
      {"_REGISTER_WIDTH", hn::Lanes(TagByWidth<WIDTH, uint8_t>{})},
  };
  for (const auto &[name, value] : constants) {
    PyObject *py_value = PyLong_FromLong(value);
    if (py_value == nullptr || PyModule_AddObject(m, name, py_value) < 0) {
      Py_XDECREF(py_value);
      Py_DECREF(m);
      return nullptr;
    }
  }

  using PreciseTypes =
      std::tuple<decltype(Precise{}), decltype(Precise{kLowAccuracy})>;

  // clang-format off
  using VecTypes = VecsByWidth<WIDTH,
      float
    #if HWY_HAVE_FLOAT64
    , double
    #endif
  >;
  // clang-format on

  if (!LoadPreciseIntrinsics(m, PreciseTypes{}, VecTypes{})) {
    Py_DECREF(m);
    return nullptr;
  }
  if (!LoadIntrinsics(m, VecTypes{})) {
    Py_DECREF(m);
    return nullptr;
  }
  if (!LoadHelpersIntrinsics(m, VecTypes{})) {
    Py_DECREF(m);
    return nullptr;
  }
  return m;
#else
  escape = true;
  return nullptr;
#endif
}

template PyObject *Target<0>(bool &);

}  // namespace npsr::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace npsr {

HWY_EXPORT_T(Target0, Target<0>);

extern "C" {
PyMODINIT_FUNC PyInit__intrins(void) {
  static PyMethodDef methods[] = {
      {NULL, NULL, 0, NULL},
  };
  static struct PyModuleDef defs = {PyModuleDef_HEAD_INIT, "npsr",
                                    "NPsr testing framework", -1, methods};

  PyObject *m = PyModule_Create(&defs);
  if (m == NULL) {
    return NULL;
  }
  PyObject *targets = PyDict_New();
  if (targets == NULL) {
    goto err;
  }
  if (PyModule_AddObject(m, "__targets__", targets) < 0) {
    Py_DECREF(targets);
    goto err;
  }
  for (uint64_t target : hwy::SupportedAndGeneratedTargets()) {
    hwy::SetSupportedTargetsForTest(target);
    hwy::GetChosenTarget().Update(hwy::SupportedTargets());
    bool escape = false;
    PyObject *target_mod = HWY_DYNAMIC_DISPATCH(Target0)(escape);
    if (target_mod == nullptr) {
      if (escape) {
        // due to fma not being supported
        continue;
      }
      goto err;
    }
    if (PyDict_SetItemString(targets, hwy::TargetName(target), target_mod) <
        0) {
      Py_DECREF(target_mod);
      goto err;
    }
  }
  return m;
err:
  Py_DECREF(m);
  return nullptr;
}
}  // namespace npsr
}  // namespace npsr
#endif  // HWY_ONCE
