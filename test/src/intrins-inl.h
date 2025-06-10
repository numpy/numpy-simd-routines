#if defined(NPSR_TEST_SRC_INTRINS_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TEST_SRC_INTRINS_INL_H_
#undef NPSR_TEST_SRC_INTRINS_INL_H_
#else
#define NPSR_TEST_SRC_INTRINS_INL_H_
#endif

#include <Python.h>
#include <hwy/highway.h>

#include <algorithm>
#include <array>
#include <tuple>
#include <type_traits>

#include "npsr/npsr.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE::test {
namespace hn = hwy::HWY_NAMESPACE;

// Helper to detect if type is instantiation of Precise
template <typename T>
struct IsPrecise : std::false_type {};

template <typename... Args>
struct IsPrecise<Precise<Args...>> : std::true_type {};

template <typename TPrecise>
static constexpr bool kHasPrecise = IsPrecise<TPrecise>::value;

template <bool HasPrecise, typename _TPrecise, typename... Args>
struct FilterPrecise {
  using TPrecise = hn::DFromV<_TPrecise>;
  using Tags =
      std::tuple<TPrecise, hn::DFromV<std::remove_reference_t<Args>>...>;
};
template <typename _TPrecise, typename... Args>
struct FilterPrecise<true, _TPrecise, Args...> {
  using Tags = std::tuple<hn::DFromV<std::remove_reference_t<Args>>...>;
  using TPrecise = _TPrecise;
};

// Helper to extract function signature from intrinsic pointer
template <auto IntrinPtr>
struct IntrinsicOverload;

// Specialization for function pointers
template <typename __TPrecise, typename VRet, typename... Args,
          VRet (*IntrinPtr)(__TPrecise, Args...)>
struct IntrinsicOverload<IntrinPtr> {
  using _TPrecise = std::remove_reference_t<__TPrecise>;
  using Filter = FilterPrecise<kHasPrecise<_TPrecise>, _TPrecise, Args...>;
  using Tags = typename Filter::Tags;
  using TPrecise = typename Filter::TPrecise;

  static constexpr size_t kNumArgs = std::tuple_size_v<Tags>;
  static constexpr bool kEscapePrecise = !kHasPrecise<TPrecise>;

  static PyObject *Call(PyObject *self, PyObject *args) {
    return CallIs(self, args, std::make_index_sequence<kNumArgs>{}, Tags{});
  }

  template <size_t... Is, typename... Tag>
  static PyObject *CallIs(PyObject *self, PyObject *args,
                          std::index_sequence<Is...>, std::tuple<Tag...>) {
    Py_ssize_t length = PySequence_Fast_GET_SIZE(args);
    if (length != kNumArgs) {
      PyErr_Format(PyExc_TypeError, "expected %zu arguments, got %zd", kNumArgs,
                   length);
      return nullptr;
    }
    using TagsU8 = std::tuple<hn::Repartition<uint8_t, Tag>...>;

    PyObject **items = PySequence_Fast_ITEMS(args);
    if (!(PyByteArray_Check(items[Is]) && ...)) {
      PyErr_Format(PyExc_TypeError, "all arguments must be bytearray");
      return nullptr;
    }

    PyByteArrayObject *arrays[] = {
        reinterpret_cast<PyByteArrayObject *>(items[Is])...};
    Py_ssize_t sizes[] = {PyByteArray_Size(items[Is])...};
    Py_ssize_t lanes[] = {static_cast<Py_ssize_t>(
        hn::Lanes(std::tuple_element_t<Is, TagsU8>{}))...};

    // Validate each array individually
    if (((sizes[Is] < lanes[Is] || sizes[Is] % lanes[Is] != 0) || ...)) {
      PyErr_Format(PyExc_ValueError,
                   "each array size must be aligned and >= its lane count, %d");
      return nullptr;
    }
    const hn::Repartition<uint8_t, hn::DFromV<VRet>> uddst;
    Py_ssize_t dst_lanes = hn::Lanes(uddst);
    Py_ssize_t result_size = sizes[0];
    PyObject *dst_obj = PyByteArray_FromStringAndSize(NULL, 0);
    if (dst_obj == nullptr) {
      return nullptr;
    }
    if (PyByteArray_Resize(dst_obj, result_size) < 0) {
      Py_DECREF(dst_obj);
      return nullptr;
    }

    uint8_t *src[] = {
        reinterpret_cast<uint8_t *>(PyByteArray_AS_STRING(arrays[Is]))...};
    uint8_t *dst = reinterpret_cast<uint8_t *>(PyByteArray_AS_STRING(dst_obj));

    // Track offsets for each source array using parameter pack
    // std::array<Py_ssize_t, kNumArgs> src_offsets{};
    // Py_ssize_t dst_offset = 0;

    TPrecise precise{};
    // Process min_iterations worth of data
    for (Py_ssize_t iter = 0; iter < result_size; iter += dst_lanes) {
      // Load from each source at its current offset
      // clang-format off
      VRet ret;
      if constexpr (kHasPrecise<TPrecise>) {
        ret = IntrinPtr(
            precise,
            hn::BitCast(
                std::tuple_element_t<Is, Tags>{},
                hn::LoadU(
                  std::tuple_element_t<Is, TagsU8>{},
                  src[Is] + iter 
                )
            )
        ...);
      }
      else {
        ret = IntrinPtr(
            hn::BitCast(
                std::tuple_element_t<Is, Tags>{},
                hn::LoadU(
                  std::tuple_element_t<Is, TagsU8>{},
                  src[Is] + iter 
                )
            )
        ...);
      }
      // clang-format on
      hn::StoreU(hn::BitCast(uddst, ret), uddst, dst + iter);
    }
    return dst_obj;
  }

  static PyObject *OverloadInfo() { return OverloadInfo_(Tags{}); }

  template <typename... Tag>
  static PyObject *OverloadInfo_(std::tuple<Tag...>) {
    constexpr size_t kNumItems = 2 + kNumArgs;
    return PyTuple_Pack(kNumItems, GetTagTypeInfo<DFromV<VRet>, true>(),
                        GetPrecise<TPrecise>(), GetTagTypeInfo<Tag>()...);
  }

  template <typename Tag, bool IS_RET = false>
  static PyObject *GetTagTypeInfo() {
    using T = hn::TFromD<Tag>;
    constexpr std::pair<const char *, int> kInfo[] = {
        {"kIsRet", static_cast<int>(IS_RET)},
        {"kLanes", hn::Lanes(Tag{})},
        {"kTypeSize", static_cast<int>(sizeof(T))},
        {"kIsUnsigned", std::is_unsigned_v<T> ? 1 : 0},
        {"kIsSigned", std::is_signed_v<T> ? 1 : 0},
        {"kIsFloat", std::is_floating_point_v<T> ? 1 : 0},
        {"kIsInteger", std::is_integral_v<T> ? 1 : 0},
    };
    PyObject *info = PyDict_New();
    if (info == nullptr) {
      return nullptr;
    }
    for (const auto &[key_name, key_val] : kInfo) {
      PyObject *py_key = PyUnicode_FromString(key_name);
      PyObject *py_val = PyLong_FromLong(key_val);
      if (py_key == nullptr || py_val == nullptr ||
          PyDict_SetItem(info, py_key, py_val) < 0) {
        Py_XDECREF(py_key);
        Py_XDECREF(py_val);
        Py_DECREF(info);
        return nullptr;
      }
      Py_DECREF(py_key);
      Py_DECREF(py_val);
    }
    return info;
  }
  template <typename TPrec>
  static std::enable_if_t<kHasPrecise<TPrec>, PyObject *> GetPrecise() {
    constexpr std::pair<bool, const char *> kPreciseOptions[] = {
        {true, "kIsPrecise"},
        {TPrec::kLowAccuracy, "kLowAccuracy"},
        {TPrec::kNoLargeArgument, "kNoLargeArgument"},
        {TPrec::kNoSpecialCases, "kNoSpecialCases"},
        {TPrec::kNoExceptions, "kNoExceptions"},
        {TPrec::kRoundForce, "kRoundForce"},
        {TPrec::kRoundNearest, "kRoundNearest"},
        {TPrec::kRoundZero, "kRoundZero"},
        {TPrec::kRoundDown, "kRoundDown"},
        {TPrec::kRoundUp, "kRoundUp"},
        {TPrec::kDAZ, "kDAZ"},
        {TPrec::kFTZ, "kFTZ"},
        {TPrec::kIEEE754, "kIEEE754"},
    };
    PyObject *preciseOptions = PyDict_New();
    if (preciseOptions == nullptr) {
      return nullptr;
    }
    for (const auto &[is_enabled, option_name] : kPreciseOptions) {
      PyObject *py_name = PyUnicode_FromString(option_name);
      PyObject *py_value = is_enabled ? Py_True : Py_False;
      Py_INCREF(py_value);  // Increment ref count for Py_True/Py_False

      if (py_name == nullptr ||
          PyDict_SetItem(preciseOptions, py_name, py_value) < 0) {
        Py_XDECREF(py_name);
        Py_XDECREF(py_value);
        Py_DECREF(preciseOptions);
        return nullptr;
      }
      Py_DECREF(py_name);
      Py_DECREF(py_value);
    }
    return preciseOptions;
  }
  template <typename Tag>
  static std::enable_if_t<!kHasPrecise<Tag>, PyObject *> GetPrecise() {
    return Py_None;
  }
};

template <auto... IntrinPtrs>
inline bool AttachIntrinsic(PyObject *m, const char *name) {
  // Create static method definitions that persist
  static std::string name_storage(name);
  static PyMethodDef methods[] = {
      {name_storage.c_str(),
       static_cast<PyCFunction>(IntrinsicOverload<IntrinPtrs>::Call),
       METH_VARARGS, "Intrinsic function overload"}...,
      {nullptr, nullptr, 0, nullptr}  // Sentinel
  };

  // Check if attribute already exists
  PyObject *existing_list = nullptr;
  bool is_new_list = true;

  if (PyObject_HasAttrString(m, name)) {
    existing_list = PyObject_GetAttrString(m, name);
    if (existing_list && PyList_Check(existing_list)) {
      is_new_list = false;
    } else {
      Py_XDECREF(existing_list);
      existing_list = nullptr;
    }
  }

  PyObject *overload_list;
  if (is_new_list) {
    overload_list = PyList_New(0);
    if (!overload_list) {
      return false;
    }
  } else {
    overload_list = existing_list;
  }
  // Create overload info array
  PyObject *overloads_info[] = {
      IntrinsicOverload<IntrinPtrs>::OverloadInfo()..., nullptr};
  // Create functions and tuples
  for (size_t i = 0; i < sizeof...(IntrinPtrs); ++i) {
    PyObject *func = PyCFunction_New(&methods[i], nullptr);
    if (!func) {
      goto cleanup_fail;
    }
    PyObject *tuple = PyTuple_Pack(2, func, overloads_info[i]);
    if (!tuple) {
      Py_DECREF(func);
      goto cleanup_fail;
    }

    if (PyList_Append(overload_list, tuple) < 0) {
      Py_DECREF(tuple);
      goto cleanup_fail;
    }
    Py_DECREF(tuple);  // PyList_Append increments ref count
  }

  // Set attribute only for new lists
  if (is_new_list) {
    if (PyObject_SetAttrString(m, name, overload_list) < 0) {
      Py_DECREF(overload_list);
      return false;
    }
    Py_DECREF(overload_list);  // Module holds the reference now
  }
  return true;

cleanup_fail:
  // Clean up any allocated overload info objects
  for (size_t i = 0; i < sizeof...(IntrinPtrs); ++i) {
    Py_XDECREF(overloads_info[i]);
  }
  if (is_new_list) {
    Py_DECREF(overload_list);
  }
  return false;
}
}  // namespace npsr::HWY_NAMESPACE::test
HWY_AFTER_NAMESPACE();
#endif  // NPSR_TEST_SRC_INTRINS_INL_H_
