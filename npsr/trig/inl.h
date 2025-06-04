#include "npsr/common.h"

#include "npsr/sincos/large-inl.h"
#include "npsr/sincos/small-inl.h"

// clang-format off
#if defined(NPSR_TRIG_INL_H_) == defined(HWY_TARGET_TOGGLE) // NOLINT
#ifdef NPSR_TRIG_INL_H_
#undef NPSR_TRIG_INL_H_
#else
#define NPSR_TRIG_INL_H_
#endif

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::sincos {
template <bool IS_COS, typename V, typename Prec>
HWY_API V SinCos(Prec &prec, V x) {
  using namespace hwy::HWY_NAMESPACE;
  constexpr bool kIsSingle = std::is_same_v<TFromV<V>, float>;
  const DFromV<V> d;
  V ret;
  if constexpr (Prec::kLowAccuracy) {
    ret = SmallArgLow<IS_COS>(x);
  }
  else {
    ret = SmallArg<IS_COS>(x);
  }
  if constexpr (Prec::kLargeArgument) {
    // Identify inputs requiring extended precision (very large arguments)
    auto has_large_arg = Gt(Abs(x), Set(d, kIsSingle ? 10000.0 : 16777216.0));
    if (HWY_UNLIKELY(!AllFalse(d, has_large_arg))) {
      // Use extended precision algorithm for large arguments
      ret = IfThenElse(has_large_arg, LargeArg<IS_COS>(x), ret);
    }
  }
  if constexpr (Prec::kSpecialCases || Prec::kException) {
    auto is_finite = IsFinite(x);
    ret = IfThenElse(is_finite, ret, NaN(d));
    if constexpr (Prec::kException) {
      prec.Raise(!AllFalse(d, IsInf(x)) ? FPExceptions::kInvalid : 0);
    }
  }
  return ret;
}
} // namespace npsr::HWY_NAMESPACE::sincos

namespace npsr::HWY_NAMESPACE {
template <typename V, typename Prec>
HWY_API V Sin(Prec &prec, V x) {
  return sincos::SinCos<false>(prec, x);
}
template <typename V, typename Prec>
HWY_API V Cos(Prec &prec, V x) {
  return sincos::SinCos<true>(prec, x);
}
} // namespace npsr::HWY_NAMESPACE

HWY_AFTER_NAMESPACE();

#endif // NPSR_TRIG_INL_H_
