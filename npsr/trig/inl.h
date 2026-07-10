// Main trigonometric function dispatcher for Highway SIMD library
// This file provides the public API for sine and cosine functions with
// configurable precision, special case handling, and algorithm selection
//
// The implementation automatically selects between three algorithms:
// 1. Low precision: ~2-3 ULP error, fastest
// 2. High precision: ~1 ULP error, moderate speed
// 3. Extended precision: Payne-Hanek reduction for huge |x| (> 10^4 float / > 2^24 double)

#if defined(NPSR_TRIG_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TRIG_INL_H_
#undef NPSR_TRIG_INL_H_
#else
#define NPSR_TRIG_INL_H_
#endif

#include "npsr/hwy.h"
#include "npsr/precise.h"
#include "npsr/trig/extended-inl.h"  // Payne-Hanek reduction for huge arguments
#include "npsr/trig/high-inl.h"      // High precision with table lookup
#include "npsr/trig/low-inl.h"       // Fast low precision implementation

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::trig {
/**
 * @brief Unified sine/cosine implementation with configurable precision
 *
 * This template function dispatches to the appropriate algorithm based on:
 * - Precision requirements (Low vs High accuracy)
 * - Input magnitude (standard vs extended precision for large arguments)
 * - Special case handling (NaN, Inf)
 *
 * @tparam OP       Operation type: kSin or kCos
 * @tparam Prec     Precise configuration class with accuracy/feature flags
 * @tparam V        Highway vector type
 *
 * @param prec      Precise object controlling FP environment and exceptions
 * @param x         Input vector
 * @return          sin(x) or cos(x) depending on OP
 *
 * Algorithm selection:
 * 1. If kLowAccuracy: Use Low<> (Cody-Waite with minimal polynomial)
 * 2. Otherwise: Use High<> (π/16 reduction with table lookup)
 * 3. If kLargeArgument and |x| > threshold: Override with Extended<>
 *
 * Thresholds for extended precision:
 * - Float: |x| > 10,000 (empirically chosen for accuracy)
 * - Double: |x| > 2^24 (16,777,216 - beyond this the range reduction's
 *   multi-word n*π products stop being exactly representable)
 */
template <Operation OP, typename Prec, typename V>
NPSR_INTRIN V Trig(Prec& prec, V x) {
  using namespace hwy::HWY_NAMESPACE;
  constexpr bool kIsSingle = std::is_same_v<TFromV<V>, float>;
  const DFromV<V> d;
  V ret;
  // Step 1: Select base algorithm based on accuracy requirements
  if constexpr (Prec::kLowAccuracy) {
    // Low precision: Cody-Waite reduction (degree-9 poly for f32, degree-15 for f64)
    // Error: ~2 ULP (~3 ULP for f64 cosine)
    ret = Low<OP>(x);
  } else {
    // High precision: π/16 reduction with table lookup + polynomial
    // Error: ~1 ULP
    ret = High<OP>(x);
  }
  // Step 2: Handle special cases (NaN, Inf) if enabled
  auto is_finite = IsFinite(x);
  if constexpr (Prec::kSpecialCases) {
    // IEEE 754 requires: sin(±∞) = NaN, cos(±∞) = NaN
    ret = IfThenElse(is_finite, ret, NaN(d));
    // -0.0 should return -0.0 for sine
    if constexpr (OP == Operation::kSin) {
      ret = IfThenElse(Eq(x, Set(d, 0.0)), x, ret);
    }
  }
  // Step 3: Handle very large arguments if enabled
  // For |x| > threshold, standard algorithms lose precision due to
  // catastrophic cancellation in x - n*π reduction
  if constexpr (Prec::kLargeArgument) {
    // Thresholds chosen based on when the per-path range reduction breaks
    // down (its n*π products stop being exactly representable), not on
    // mantissa resolution:
    // - Float: 10,000 is conservative but keeps the widened path < 1 ULP.
    // - Double: beyond 2^24 the reduction's n*π products stop being exact
    //   (for the π/16 high-precision path the quotient |round(x*16/π)| would
    //   exceed round(2^24*16/π)), so x - n*π stops being faithful.
    // - Double low-accuracy cosine:  stays on its fast path
    //   until trunc(|x|/π) > 8388606; this is the largest double below that
    //   boundary (~8388607π)
    constexpr bool kIsLowCos =
        Prec::kLowAccuracy && OP == trig::Operation::kCos;
    constexpr double kLargeF64 = kIsLowCos ? 0x1.921fb2200366fp+24 : 16777216.0;
    auto has_large_arg =
        And(Gt(Abs(x), Set(d, kIsSingle ? 10000.0f : kLargeF64)), is_finite);

    // Extended precision is expensive, only use when necessary
    if (HWY_UNLIKELY(!AllFalse(d, has_large_arg))) {
      // Payne-Hanek reduction: Uses ~96-bit (float) or ~192-bit (double)
      // precision for 4/π to maintain accuracy for huge arguments
      ret = IfThenElse(has_large_arg, Extended<OP>(x), ret);
    }
  }
  // Step 4: Raise invalid operation exception for infinity inputs
  if constexpr (Prec::kExceptions) {
    prec.Raise(!AllFalse(d, IsInf(x)) ? FPExceptions::kInvalid : 0);
  }
  return ret;
}

}  // namespace npsr::HWY_NAMESPACE::trig

// Public API in the main npsr namespace
namespace npsr::HWY_NAMESPACE {

/**
 * @brief Compute sine of vector elements with configurable precision
 *
 * @tparam Prec  Precise configuration (e.g., Precise{kLowAccuracy})
 * @tparam V     Highway vector type
 * @param prec   Precise object managing FP environment
 * @param x      Input vector
 * @return       sin(x) for each element
 *
 * @example
 * ```cpp
 * Precise prec{
 *  kLowAccuracy, kNoLargeArgument, kNoExceptions, kNoSpecialCases
 * };
 * auto result = Sin(prec, input_vector);
 * ```
 */
template <typename Prec, typename V>
NPSR_INTRIN V Sin(Prec& prec, V x) {
  return trig::Trig<trig::Operation::kSin>(prec, x);
}

/**
 * @brief Compute cosine of vector elements with configurable precision
 *
 * @tparam Prec  Precise configuration (e.g., Precise{kLowAccuracy})
 * @tparam V     Highway vector type
 * @param prec   Precise object managing FP environment
 * @param x      Input vector
 * @return       cos(x) for each element
 *
 * @example
 * ```cpp
 * Precise prec{
 *  kLowAccuracy, kNoLargeArgument, kNoExceptions, kNoSpecialCases
 * };
 * auto result = Cos(prec, input_vector);
 * ```
 */
template <typename Prec, typename V>
NPSR_INTRIN V Cos(Prec& prec, V x) {
  return trig::Trig<trig::Operation::kCos>(prec, x);
}

}  // namespace npsr::HWY_NAMESPACE

HWY_AFTER_NAMESPACE();

#endif  // NPSR_TRIG_INL_H_
