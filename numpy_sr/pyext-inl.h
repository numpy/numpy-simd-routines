// Per-target kernel driver + Operation builder, re-expanded once per SIMD
// target via hwy/foreach_target.h.
#if defined(NPSR_PY_PYEXT_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_PY_PYEXT_INL_H_
#undef NPSR_PY_PYEXT_INL_H_
#else
#define NPSR_PY_PYEXT_INL_H_
#endif

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "npsr/npsr.h"
#include "numpy_sr/pyext.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::py::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// LoadN/StoreN handle the tail.
template <typename Prec, typename T, typename Op>
void UnaryForward(const T* src, T* dst, size_t n) {
  const hn::ScalableTag<T> d;
  Prec prec;
  Op op;

  const size_t lanes = hn::Lanes(d);
  size_t i = 0;
  for (; i + lanes <= n; i += lanes) {
    hn::StoreU(op(prec, hn::LoadU(d, src + i)), d, dst + i);
  }
  if (i < n) {
    hn::StoreN(op(prec, hn::LoadN(d, src + i, n - i)), d, dst + i, n - i);
  }
}

// Profiles every op is built for; see PrecBit.
using PHigh = Precise<>;
using PLow = decltype(Precise{kLowAccuracy});
using PHighNoExc = decltype(Precise{kNoExceptions});
using PHighFast = decltype(Precise{kNoLargeArgument, kNoSpecialCases});
using PLowFast = decltype(Precise{kLowAccuracy, kNoLargeArgument,
                                  kNoSpecialCases, kNoExceptions});

template <class T>
constexpr TypeID TypeIdOf() {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "unsupported element type");
  return std::is_same_v<T, float> ? TypeID::kFloat32 : TypeID::kFloat64;
}

// One FuncData row per element type in `Ts`.
template <class F, class Prec, class... Ts>
void AddRows(Operation& op) {
  ((op.data[op.ndata++] =
        FuncData{reinterpret_cast<uintptr_t>(&UnaryForward<Prec, Ts, F>),
                 ::npsr::py::PrecMask<Prec>(), TypeIdOf<Ts>()}),
   ...);
}

template <class F>
Operation MakeUnaryOperation(const char* name) {
  Operation op{name, FuncID::kUnary, HWY_TARGET, 0, {}};
  // double rows only where the target has native f64 lanes (e.g. not Armv7
  // NEON); their absence is what makes the Python side skip f64 for a target.
#if HWY_HAVE_FLOAT64
#define NPSR_PY_FLOATS float, double
#else
#define NPSR_PY_FLOATS float
#endif
  AddRows<F, PHigh, NPSR_PY_FLOATS>(op);
  AddRows<F, PLow, NPSR_PY_FLOATS>(op);
  AddRows<F, PHighNoExc, NPSR_PY_FLOATS>(op);
  AddRows<F, PHighFast, NPSR_PY_FLOATS>(op);
  AddRows<F, PLowFast, NPSR_PY_FLOATS>(op);
#undef NPSR_PY_FLOATS
  return op;
}

}  // namespace npsr::py::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // NPSR_PY_PYEXT_INL_H_
