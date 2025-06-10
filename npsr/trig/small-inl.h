#include "npsr/common.h"
#include "npsr/trig/data/small.h"
#include "npsr/utils-inl.h"

#if defined(NPSR_TRIG_SMALL_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TRIG_SMALL_INL_H_
#undef NPSR_TRIG_SMALL_INL_H_
#else
#define NPSR_TRIG_SMALL_INL_H_
#endif

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::sincos {

using namespace hwy;
using namespace hwy::HWY_NAMESPACE;

template <bool IS_COS, typename V, HWY_IF_F32(TFromV<V>)>
HWY_API V SmallPolyLow(V r, V r2) {
  const DFromV<V> d;
  const V c9 = Set(d, IS_COS ? 0x1.5d866ap-19f : 0x1.5dbdfp-19f);
  const V c7 = Set(d, IS_COS ? -0x1.9f6d9ep-13 : -0x1.9f6ffep-13f);
  const V c5 = Set(d, IS_COS ? 0x1.110ec8p-7 : 0x1.110eccp-7f);
  const V c3 = Set(d, -0x1.55554cp-3f);
  V poly = MulAdd(c9, r2, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  if constexpr (IS_COS) {
    // Although this path handles cosine, we have already transformed the
    // input using the identity: cos(x) = sin(x + π/2) This means we're no
    // longer directly evaluating a cosine Taylor series; instead, we evaluate
    // the sine approximation polynomial at (x + π/2).
    //
    // The sine approximation has the general form:
    //    sin(r) ≈ r + r³ · P(r²)
    //
    // So, we compute:
    //    r³ = r · r²
    //    sin(r) ≈ r + r³ · poly
    //
    // This formulation preserves accuracy by computing the highest order
    // terms last, which benefits from FMA to reduce rounding error.
    V r3 = Mul(r2, r);
    poly = MulAdd(r3, poly, r);
  } else {
    poly = Mul(poly, r2);
    poly = MulAdd(r, poly, r);
  }
  return poly;
}

template <bool IS_COS, typename V, HWY_IF_F64(TFromV<V>)>
HWY_API V SmallPolyLow(V r, V r2) {
  const DFromV<V> d;
  const V c15 = Set(d, -0x1.9f1517e9f65fp-41);
  const V c13 = Set(d, 0x1.60e6bee01d83ep-33);
  const V c11 = Set(d, -0x1.ae6355aaa4a53p-26);
  const V c9 = Set(d, 0x1.71de3806add1ap-19);
  const V c7 = Set(d, -0x1.a01a019a659ddp-13);
  const V c5 = Set(d, 0x1.111111110a573p-7);
  const V c3 = Set(d, -0x1.55555555554a8p-3);

  V poly = MulAdd(c15, r2, c13);
  poly = MulAdd(r2, poly, c11);
  poly = MulAdd(r2, poly, c9);
  poly = MulAdd(r2, poly, c7);
  poly = MulAdd(r2, poly, c5);
  poly = MulAdd(r2, poly, c3);
  return poly;
}

template <bool IS_COS, typename V>
HWY_API V SmallArgLow(V x) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  using T = TFromV<V>;
  // Load frequently used constants as vector registers
  const V abs_mask = BitCast(d, Set(du, SignMask<T>() - 1));
  const V x_abs = And(abs_mask, x);
  const V x_sign = AndNot(x_abs, x);

  constexpr bool kIsSingle = std::is_same_v<T, float>;
  // Transform cosine to sine using identity: cos(x) = sin(x + π/2)
  const V half_pi = Set(d, kIsSingle ? 0x1.921fb6p0f : 0x1.921fb54442d18p0);
  V x_trans = x_abs;
  if constexpr (IS_COS) {
    x_trans = Add(x_abs, half_pi);
  }
  // check zero input/subnormal for cosine (cos(~0) = 1)
  const auto is_cos_near_zero = Eq(x_trans, half_pi);

  // Compute N = round(x/π) using "magic number" technique
  // and stores integer part in mantissa
  const V inv_pi = Set(d, kIsSingle ? 0x1.45f306p-2f : 0x1.45f306dc9c883p-2);
  const V magic_round = Set(d, kIsSingle ? 0x1.8p23f : 0x1.8p52);
  V n_biased = MulAdd(x_trans, inv_pi, magic_round);
  V n = Sub(n_biased, magic_round);
  // Adjust quotient for cosine (accounts for π/2 phase shift)
  if constexpr (IS_COS) {
    // For cosine, we computed N = round((x + π/2)/π) but need N' for x:
    //   N = round((x + π/2)/π) = round(x/π + 0.5)
    // This is often 1 more than round(x/π), so we subtract 0.5:
    //   N' = N - 0.5
    n = Sub(n, Set(d, static_cast<T>(0.5)));
  }
  // Use Cody-Waite method with triple-precision PI
  const V pi_hi = Set(d, kIsSingle ? 0x1.921fb6p1f : 0x1.921fb54442d18p+1);
  const V pi_med =
      Set(d, kIsSingle ? -0x1.777a5cp-24f : 0x1.c1cd129024e09p-106);
  const V pi_lo = Set(d, kIsSingle ? -0x1.ee59dap-49f : 0x1.1a62633145c06p-53);
  V r = NegMulAdd(n, pi_hi, x_abs);
  if constexpr (kIsSingle) {
    r = NegMulAdd(n, pi_med, r);
  }
  r = NegMulAdd(n, pi_lo, r);
  V r2 = Mul(r, r);
  V poly = SmallPolyLow<IS_COS>(r, r2);
  if constexpr (!kIsSingle) {
    V r_mid = NegMulAdd(n, pi_med, r);
    V r2_corr = Mul(r2, r_mid);
    poly = MulAdd(r2_corr, poly, r_mid);
  }
  // Extract octant sign information from quotient and flip the sign bit
  poly = Xor(poly,
             BitCast(d, ShiftLeft<sizeof(T) * 8 - 1>(BitCast(du, n_biased))));
  if constexpr (IS_COS) {
    poly = IfThenElse(is_cos_near_zero, Set(d, static_cast<T>(1.0)), poly);
  } else {
    // Restore original sign for sine (odd function)
    poly = Xor(poly, x_sign);
  }
  return poly;
}

template <bool IS_COS, typename V, HWY_IF_F32(TFromV<V>)>
HWY_INLINE V SmallArg(V x) {
  using T = TFromV<V>;
  using D = DFromV<V>;
  using DU = RebindToUnsigned<D>;
  using DH = Half<D>;
  using DW = RepartitionToWide<D>;
  using VW = Vec<DW>;

  const D d;
  const DU du;
  const DH dh;
  const DW dw;
  // Load frequently used constants as vector registers
  const V abs_mask = BitCast(d, Set(du, 0x7FFFFFFF));
  const V x_abs = And(abs_mask, x);
  const V x_sign = AndNot(x_abs, x);

  // Transform cosine to sine using identity: cos(x) = sin(x + π/2)
  const V half_pi = Set(d, 0x1.921fb6p0f);
  V x_trans = x_abs;
  if constexpr (IS_COS) {
    x_trans = Add(x_abs, half_pi);
  }
  // check zero input/subnormal for cosine (cos(~0) = 1)
  const auto is_cos_near_zero = Eq(x_trans, half_pi);

  // Compute N = round(input/π) using "magic number" technique
  // Adding 2^23 forces rounding and stores integer part in mantissa
  const V inv_pi = Set(d, 0x1.45f306p-2f);
  const V magic_round = Set(d, 0x1.8p23f);
  V n_biased = MulAdd(x_trans, inv_pi, magic_round);
  V n = Sub(n_biased, magic_round);

  // Adjust quotient for cosine (accounts for π/2 phase shift)
  if constexpr (IS_COS) {
    // For cosine, we computed N = round((x + π/2)/π) but need N' for x:
    //   N = round((x + π/2)/π) = round(x/π + 0.5)
    // This is often 1 more than round(x/π), so we subtract 0.5:
    //   N' = N - 0.5
    n = Sub(n, Set(d, 0.5f));
  }
  auto WideCal = [](VW nh, VW xh_abs) -> VW {
    const DFromV<VW> dw;
    const VW pi_hi = Set(dw, 0x1.921fb5444p1);
    const VW pi_lo = Set(dw, 0x1.68c234c4c6629p-38);

    VW r = NegMulAdd(nh, pi_lo, NegMulAdd(nh, pi_hi, xh_abs));
    VW r2 = Mul(r, r);

    // Polynomial coefficients for sin(r) approximation on [-π/2, π/2]
    const VW c9 = Set(dw, 0x1.5dbdf0e4c7deep-19);
    const VW c7 = Set(dw, -0x1.9f6ffeea73463p-13);
    const VW c5 = Set(dw, 0x1.110ed3804ca96p-7);
    const VW c3 = Set(dw, -0x1.55554bc836587p-3);

    VW poly = MulAdd(c9, r2, c7);
    poly = MulAdd(r2, poly, c5);
    poly = MulAdd(r2, poly, c3);
    poly = Mul(poly, r2);
    poly = MulAdd(r, poly, r);
    return poly;
  };

  VW poly_lo = WideCal(PromoteLowerTo(dw, n), PromoteLowerTo(dw, x_abs));
  VW poly_up = WideCal(PromoteUpperTo(dw, n), PromoteUpperTo(dw, x_abs));
  V poly = Combine(d, DemoteTo(dh, poly_up), DemoteTo(dh, poly_lo));
  // Extract octant sign information from quotient and flip the sign bit
  poly = Xor(poly,
             BitCast(d, ShiftLeft<sizeof(T) * 8 - 1>(BitCast(du, n_biased))));
  if constexpr (IS_COS) {
    poly = IfThenElse(is_cos_near_zero, Set(d, 1.0f), poly);
  } else {
    // Restore original sign for sine (odd function)
    poly = Xor(poly, x_sign);
  }
  return poly;
}
/**
 * This function computes sin(x) or cos(x) for |x| < 2^24 using the Cody-Waite
 * reduction algorithm combined with table lookup and polynomial approximation,
 * achieves < 1 ULP error for |x| < 2^24.
 *
 * Algorithm Overview:
 * 1. Range Reduction: Reduces input x to r where |r| < π/16
 *    - Computes n = round(x * 16/π) and r = x - n*π/16
 *    - Uses multi-precision arithmetic (3 parts of π/16) for accuracy
 *
 * 2. Table Lookup: Retrieves precomputed sin(n*π/16) and cos(n*π/16)
 *    - Includes high and low precision parts for cos values
 *
 * 3. Polynomial Approximation: Computes sin(r) and cos(r)
 *    - sin(r) ≈ r * (1 + r²*P_sin(r²)) where P_sin is a minimax polynomial
 *    - cos(r) ≈ 1 + r²*P_cos(r²) where P_cos is a minimax polynomial
 *
 * 4. Reconstruction: Applies angle addition formulas
 *    - sin(x) = sin(n*π/16 + r) = sin(n*π/16)*cos(r) + cos(n*π/16)*sin(r)
 *    - cos(x) = cos(n*π/16 + r) = cos(n*π/16)*cos(r) - sin(n*π/16)*sin(r)
 *
 */
template <bool IS_COS, typename V, HWY_IF_F64(TFromV<V>)>
HWY_INLINE V SmallArg(V x) {
  using trig::data::kHiCosKPi16Table;
  using trig::data::kHiSinKPi16Table;
  using trig::data::kPackedLowSinCosKPi16Table;

  using T = TFromV<V>;
  using D = DFromV<V>;
  using DU = RebindToUnsigned<D>;
  using VU = Vec<DU>;

  const D d;
  const DU du;

  // Constants for range reduction
  constexpr T kInvPi = 0x1.45f306dc9c883p2;       // 16/π for range reduction
  constexpr T kPi16High = 0x1.921fb54442d18p-3;   // π/16 high precision part
  constexpr T kPi16Low = 0x1.1a62633p-57;         // π/16 low precision part
  constexpr T kPi16Tiny = 0x1.45c06e0e68948p-89;  // π/16 tiny precision part
  // Step 1: Range reduction - find n such that x = n*(π/16) + r, where |r| <
  // π/16
  V magic = Set(d, 0x1.8p52);
  V n_biased = MulAdd(x, Set(d, kInvPi), magic);
  V n = Sub(n_biased, magic);

  // Extract integer index for table lookup (n mod 16)
  VU n_int = BitCast(du, n_biased);
  VU table_idx = And(n_int, Set(du, 0xF));  // Mask to get n mod 16

  // Step 2: Load precomputed sine/cosine values for n mod 16
  V sin_hi = LutX2(kHiSinKPi16Table, table_idx);
  V cos_hi = LutX2(kHiCosKPi16Table, table_idx);
  // Note: cos_lo and sin_lo are packed together (32 bits each) to save memory.
  // cos_lo can be used as-is since it's in the upper bits, sin_lo needs
  // extraction. The precision loss is negligible for the final result.
  // see lut-inl.h.py for the table generation code.
  V cos_lo = LutX2(kPackedLowSinCosKPi16Table, table_idx);
  // Extract sin_low from packed format (upper 32 bits)
  V sin_lo = BitCast(d, ShiftLeft<32>(BitCast(du, cos_lo)));

  // Step 3: Multi-precision computation of remainder r
  V r_hi = NegMulAdd(n, Set(d, kPi16High), x);     // r = x - n*(π/16)_high
  V r_mid = NegMulAdd(n, Set(d, kPi16Low), r_hi);  // Subtract low part
  V r = NegMulAdd(n, Set(d, kPi16Tiny), r_mid);    // Subtract tiny part
  // Compute low precision part of r for extra accuracy
  V term = NegMulAdd(Set(d, kPi16Low), n, Sub(r_hi, r_mid));
  V r_lo = MulAdd(Set(d, kPi16Tiny), n, Sub(r, r_mid));
  r_lo = Sub(term, r_lo);

  // Step 4: Polynomial approximation
  V r2 = Mul(r, r);

  // Minimax polynomial for (sin(r)/r - 1)
  // sin(r)/r = 1 - r²/3! + r⁴/5! - r⁶/7! + ...
  // This polynomial computes the terms after 1
  V sin_poly = Set(d, 0x1.71c97d22a73ddp-19);
  sin_poly = MulAdd(sin_poly, r2, Set(d, -0x1.a01a00ed01edep-13));
  sin_poly = MulAdd(sin_poly, r2, Set(d, 0x1.111111110e99dp-7));
  sin_poly = MulAdd(sin_poly, r2, Set(d, -0x1.5555555555555p-3));

  // Minimax polynomial for (cos(r) - 1)/r²
  // cos(r) = 1 - r²/2! + r⁴/4! - r⁶/6! + ...
  // This polynomial computes (cos(r) - 1)/r²
  V cos_poly = Set(d, 0x1.9ffd7d9d749bcp-16);
  cos_poly = MulAdd(cos_poly, r2, Set(d, -0x1.6c16c075d73f8p-10));
  cos_poly = MulAdd(cos_poly, r2, Set(d, 0x1.555555554e8d6p-5));
  cos_poly = MulAdd(cos_poly, r2, Set(d, -0x1.ffffffffffffcp-2));

  // Step 5: Reconstruction using angle addition formulas
  //
  // Mathematical equivalence between traditional and SVML approaches:
  //
  // Traditional angle addition:
  // sin(a+r) = sin(a)*cos(r) + cos(a)*sin(r)
  // cos(a+r) = cos(a)*cos(r) - sin(a)*sin(r)
  //
  // Where for small r (|r| < π/16):
  // cos(r) ≈ 1 + r²*cos_poly
  // sin(r) ≈ r*(1 + sin_poly) ≈ r + r*sin_poly
  //
  // SVML's efficient linear approximation:
  // sin(a+r) ≈ sin(a) + cos(a)*r + polynomial_corrections
  // cos(a+r) ≈ cos(a) - sin(a)*r + polynomial_corrections
  //
  // This is mathematically equivalent but computationally more efficient:
  // - Uses first-order linear terms directly: Sh + Ch*R, Ch - R*Sh
  // - Applies higher-order polynomial corrections separately
  // - Fewer multiplications and better numerical stability
  //
  // Implementation follows SVML structure:
  // sin(n*π/16 + r) = sin_table + cos_table*remainder (+ corrections)
  // cos(n*π/16 + r) = cos_table - sin_table*remainder (+ corrections)
  V result;
  if constexpr (IS_COS) {
    // Cosine reconstruction: cos_table - sin_table*remainder
    // Equivalent to: cos(a)*cos(r) - sin(a)*sin(r) but more efficient
    V res_hi = NegMulAdd(r, sin_hi, cos_hi);  // cos_hi - r*sin_hi

    // This captures the precision lost in the main computation
    V r_sin_hi = Sub(cos_hi, res_hi);  // Extract high part of multiplication

    // Handles rounding errors and adds sin_low contribution
    V r_sin_low = MulSub(r, sin_hi, r_sin_hi);  // Compute multiplication error
    V sin_low_corr = MulAdd(r, sin_lo, r_sin_low);  // Add sin_low term

    // This is used to apply the low-precision remainder correction
    V sin_cos_r = MulAdd(r, cos_hi, sin_hi);

    // Main low precision correction: cos_low - r_low*(sin_table + cos_table*r)
    // Applies the effect of the low-precision remainder on the final result
    V low_corr = NegMulAdd(r_lo, sin_cos_r, cos_lo);

    // Polynomial corrections using the remainder
    V r_sin = Mul(r, sin_hi);  // For polynomial application

    // Apply polynomial corrections: cos_table*cos_poly - r*sin_table*sin_poly
    // This handles the higher-order terms from cos(r) and sin(r) expansions
    V poly_corr = Mul(cos_hi, cos_poly);  // cos(a) * (cos(r)-1)/r²
    // - sin(a)*r * (sin(r)/r-1)
    poly_corr = NegMulAdd(r_sin, sin_poly, poly_corr);

    // Combine all low precision corrections
    V total_low = Sub(low_corr, sin_low_corr);

    // Final assembly: main_term + r²*polynomial_corrections + low_corrections
    result = MulAdd(r2, poly_corr, total_low);
    result = Add(res_hi, result);

  } else {
    // Sine reconstruction: sin_table + cos_table*remainder
    // Equivalent to: sin(a)*cos(r) + cos(a)*sin(r) but more efficient
    V res_hi = MulAdd(r, cos_hi, sin_hi);  // sin_hi + r*cos_hi

    // This captures the precision lost in the main computation
    V r_cos_hi = Sub(res_hi, sin_hi);  // Extract high part of multiplication

    // Handles rounding errors and adds cos_low contribution
    V r_cos_low = MulSub(r, cos_hi, r_cos_hi);  // Compute multiplication error
    V cos_low_corr = MulAdd(r, cos_lo, r_cos_low);  // Add cos_low term

    // Intermediate term for r_low correction: cos_table - sin_table*r
    // This is used to apply the low-precision remainder correction
    V cos_r_sin = NegMulAdd(r, sin_hi, cos_hi);

    // Main low precision correction: sin_low - r_low*(cos_table - sin_table*r)
    // Applies the effect of the low-precision remainder on the final result
    V low_corr = MulAdd(r_lo, cos_r_sin, sin_lo);
    // Polynomial corrections using the remainder
    V r_cos = Mul(r, cos_hi);  // For polynomial application

    // Apply polynomial corrections: sin_table*cos_poly + r*cos_table*sin_poly
    // This handles the higher-order terms from cos(r) and sin(r) expansions
    V poly_corr = Mul(sin_hi, cos_poly);  // sin(a) * (cos(r)-1)/r²
    poly_corr =
        MulAdd(r_cos, sin_poly, poly_corr);  // + cos(a)*r * (sin(r)/r-1)

    // Combine all low precision corrections
    V total_low = Add(low_corr, cos_low_corr);
    // Final assembly: main_term + r²*polynomial_corrections + low_corrections
    result = MulAdd(r2, poly_corr, total_low);
    result = Add(res_hi, result);
  }

  // Apply final sign correction same for both sine and cosine
  // Both functions change sign every π radians, corresponding to bit 4 of n_int
  // This unified approach works because:
  // - sin(x + π) = -sin(x)
  // - cos(x + π) = -cos(x)
  VU x_sign_int = ShiftLeft<63>(BitCast(du, x));
  // XOR with quadrant info in n_biased
  VU combined = Xor(BitCast(du, n_biased), ShiftLeft<4>(x_sign_int));
  // Extract final sign
  VU sign = ShiftRight<4>(combined);
  sign = ShiftLeft<63>(sign);
  result = Xor(result, BitCast(d, sign));  // Apply sign flip
  return result;
}
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace npsr::HWY_NAMESPACE::sincos

HWY_AFTER_NAMESPACE();

#endif  // NPSR_TRIG_SMALL_INL_H_
