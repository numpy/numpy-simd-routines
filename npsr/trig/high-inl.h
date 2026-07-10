#if defined(NPSR_TRIG_HIGH_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TRIG_HIGH_INL_H_
#undef NPSR_TRIG_HIGH_INL_H_
#else
#define NPSR_TRIG_HIGH_INL_H_
#endif

#include "npsr/hwy.h"
#include "npsr/lut-inl.h"
#include "npsr/trig/data/data.h"
#include "npsr/trig/low-inl.h"  // Operation

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE::trig {

template <Operation OP, typename V, HWY_IF_F32(TFromV<V>)>
NPSR_INTRIN V High(V x) {
  using namespace hn;
  namespace data = ::npsr::trig::data;

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
  const V half_pi = Set(d, data::kHalfPi<T>);
  V x_trans = x_abs;
  if constexpr (OP == Operation::kCos) {
    x_trans = Add(x_abs, half_pi);
  }
  // check zero input/subnormal for cosine (cos(~0) = 1)
  const auto is_cos_near_zero = Eq(x_trans, half_pi);

  // Compute N = round(input/π)
  const V magic_round = Set(d, 0x1.8p23f);
  V n_biased = MulAdd(x_trans, Set(d, data::kInvPi<T>), magic_round);
  V n = Sub(n_biased, magic_round);

  // Adjust quotient for cosine (accounts for π/2 phase shift)
  if constexpr (OP == Operation::kCos) {
    // For cosine, we computed N = round((x + π/2)/π) but need N' for x:
    //   N = round((x + π/2)/π) = round(x/π + 0.5)
    // This is often 1 more than round(x/π), so we subtract 0.5:
    //   N' = N - 0.5
    n = Sub(n, Set(d, 0.5f));
  }
  auto WideCal = [](const VW &nh, const VW &xh_abs) -> VW {
    const DFromV<VW> dw;
    constexpr auto kPiPrec35 = data::kPiPrec35<true>;
    VW r = NegMulAdd(nh, Set(dw, kPiPrec35[0]), xh_abs);
    r = NegMulAdd(nh, Set(dw, kPiPrec35[1]), r);
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
  // sin(r) was reduced around n·π, and sin/cos flip sign every π, so the sign
  // is the parity of n, carried in bit 0 of n_biased. Shift it into the sign
  // position and XOR to flip the result on odd n.
  poly = Xor(poly,
             BitCast(d, ShiftLeft<sizeof(T) * 8 - 1>(BitCast(du, n_biased))));
  if constexpr (OP == Operation::kCos) {
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
 * achieves <= 1 ULP error for |x| < 2^24 (worst case reaches, but does not
 * exceed, 1 ULP).
 *
 * Algorithm Overview:
 * 1. Range Reduction: Reduces input x to r where |r| < π/16
 *    - Computes n = round(x * 16/π) and r = x - n*π/16
 *    - Uses multi-word π/16 (3 words with FMA, 4 without) for accuracy
 *
 * 2. Table Lookup: Retrieves precomputed sin(n*π/16) and cos(n*π/16)
 *    - Stores high and low parts for both sin and cos (lows packed 32:32)
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
template <Operation OP, typename V, HWY_IF_F64(TFromV<V>)>
NPSR_INTRIN V High(V x) {
  using namespace hn;
  namespace data = ::npsr::trig::data;
  using T = TFromV<V>;
  using D = DFromV<V>;
  using DU = RebindToUnsigned<D>;
  using VU = Vec<DU>;

  const D d;
  const DU du;

  // Step 1: Range reduction - find n such that x = n*(π/16) + r, where |r| <
  // π/16
  V magic = Set(d, 0x1.8p52);
  V n_biased = MulAdd(x, Set(d, data::k16DivPi<T>), magic);
  V n = Sub(n_biased, magic);

  // Extract integer index for table lookup (n mod 16)
  VU n_int = BitCast(du, n_biased);
  VU table_idx = And(n_int, Set(du, 0xF));  // Mask to get n mod 16

  // Step 2: Load precomputed sine/cosine values for n mod 16
  V sin_hi, cos_hi, cos_lo;
  kKPi16Table.Load(table_idx, sin_hi, cos_hi, cos_lo);
  // Note: cos_lo and sin_lo are packed together (32 bits each) to save memory.
  // cos_lo can be used as-is since it's in the upper bits, sin_lo needs
  // extraction. The precision loss is negligible for the final result.
  // see data/kpi16-inl.h.sol for the table generation code.
  V sin_lo = BitCast(d, ShiftLeft<32>(BitCast(du, cos_lo)));

  // Step 3: Multi-precision computation of remainder r
  // r = x - n*(π/16)_high
  // Both splits apply their parts in descending order through the same DAG.
  // Without native FMA the non-tail parts carry 27/25/29 bits. The two
  // leading products n*part are exact for |n| < 85445660 = ceil(2^24*16/π)
  // (split and bound co-tuned so n*part0 just fits in 53 bits); part2 and
  // the tail are rounded products, but their product rounding — like each
  // subtraction's — is captured exactly by the idioms below, so the decayed
  // mul+sub sequence loses nothing against the fused path.
  constexpr auto kPiDiv16Prec29 = data::kPiDiv16Prec29<kNativeFMA>;
  V r_hi = NegMulAdd(n, Set(d, kPiDiv16Prec29[0]), x);
  const V pi16_med = Set(d, kPiDiv16Prec29[1]);
  const V pi16_lo = Set(d, kPiDiv16Prec29[2]);
  V r_med = NegMulAdd(n, pi16_med, r_hi);
  V r = NegMulAdd(n, pi16_lo, r_med);

  // Compute low precision part of r for extra accuracy
  V term = NegMulAdd(pi16_med, n, Sub(r_hi, r_med));
  V r_lo = MulAdd(pi16_lo, n, Sub(r, r_med));
  r_lo = Sub(term, r_lo);
  if constexpr (!kNativeFMA) {
    // Fourth piece (48 bits at 2^-91). Reuse the one rounded product in both
    // the subtraction and its error capture so (r, r_lo) stays an exact
    // double-double of the reduction even when r is tiny near k*π/2.
    const V tail_prod = Mul(n, Set(d, kPiDiv16Prec29[3]));
    const V r_prev = r;
    r = Sub(r_prev, tail_prod);
    r_lo = Add(r_lo, Sub(Sub(r_prev, r), tail_prod));
  }

  // Step 4: Polynomial approximation
  V r2 = Mul(r, r);

  // Minimax polynomial for (sin(r)/r - 1)/r²  ==  (sin(r) - r)/r³
  // The Taylor form sin(r)/r - 1 = -r²/3! + r⁴/5! - ... fixes the leading
  // coefficient at -1/3! = -1/6 (higher terms are minimax-adjusted); the
  // stripped r² factor is reapplied downstream via MulAdd(r2, ...).
  V sin_poly = Set(d, 0x1.71c97d22a73ddp-19);
  sin_poly = MulAdd(sin_poly, r2, Set(d, -0x1.a01a00ed01edep-13));
  sin_poly = MulAdd(sin_poly, r2, Set(d, 0x1.111111110e99dp-7));
  sin_poly = MulAdd(sin_poly, r2, Set(d, -0x1.5555555555555p-3));

  // Minimax polynomial for (cos(r) - 1)/r²
  // From cos(r) = 1 - r²/2! + r⁴/4! - ..., the leading term is ≈ -1/2! = -1/2
  // (minimax-adjusted, so not exactly -1/2).
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
  // sin(r) ≈ r + r³*sin_poly
  //
  // SVML's efficient linear approximation:
  // sin(a+r) ≈ sin(a) + cos(a)*r + polynomial_corrections
  // cos(a+r) ≈ cos(a) - sin(a)*r + polynomial_corrections
  //
  // This is mathematically equivalent but computationally more efficient:
  // - Uses first-order linear terms directly: Sh + Ch*R, Ch - R*Sh
  // - Applies higher-order polynomial corrections separately
  // - Better accuracy near the result's zeros (dominant term stays leading)
  //
  // Implementation follows SVML structure:
  // sin(n*π/16 + r) = sin_table + cos_table*remainder (+ corrections)
  // cos(n*π/16 + r) = cos_table - sin_table*remainder (+ corrections)
  V result;
  if constexpr (OP == Operation::kCos) {
    // Cosine reconstruction: cos_table - sin_table*remainder
    // Equivalent to: cos(a)*cos(r) - sin(a)*sin(r) but more efficient
    V res_hi, r_sin_low;
    if constexpr (kNativeFMA) {
      res_hi = NegMulAdd(r, sin_hi, cos_hi);  // cos_hi - r*sin_hi

      // This captures the precision lost in the main computation
      V r_sin_hi = Sub(cos_hi, res_hi);  // Extract high part of multiplication

      // Handles rounding errors via the FMA product-error idiom
      r_sin_low = MulSub(r, sin_hi, r_sin_hi);  // Compute multiplication error
    } else {
      // Exact-by-construction head product replaces the FMA idiom
      V r_sin, r_sin_rest;
      SplitMul(r, sin_hi, r_sin, r_sin_rest);
      res_hi = Sub(cos_hi, r_sin);
      V r_sin_hi = Sub(cos_hi, res_hi);
      r_sin_low = Add(Sub(r_sin, r_sin_hi), r_sin_rest);
    }
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
    V res_hi, r_cos_low;
    if constexpr (kNativeFMA) {
      res_hi = MulAdd(r, cos_hi, sin_hi);  // sin_hi + r*cos_hi

      // This captures the precision lost in the main computation
      V r_cos_hi = Sub(res_hi, sin_hi);  // Extract high part of multiplication

      // Handles rounding errors via the FMA product-error idiom
      r_cos_low = MulSub(r, cos_hi, r_cos_hi);  // Compute multiplication error
    } else {
      // Exact-by-construction head product replaces the FMA idiom
      V r_cos, r_cos_rest;
      SplitMul(r, cos_hi, r_cos, r_cos_rest);
      res_hi = Add(sin_hi, r_cos);
      V r_cos_hi = Sub(res_hi, sin_hi);
      r_cos_low = Add(Sub(r_cos, r_cos_hi), r_cos_rest);
    }
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
  // TODO(seiko2plus): x_sign_int and the Xor are dead code (always zero) and
  // should be removed. The sign is bit 4 of n_biased alone; sin(-0.0) is
  // handled in trig/inl.h under kSpecialCases.
  VU x_sign_int = ShiftLeft<63>(BitCast(du, x));
  // Fold in the π-period parity (bit 4 of n_biased); x_sign_int is 0 (dead).
  VU combined = Xor(BitCast(du, n_biased), ShiftLeft<4>(x_sign_int));
  // Extract final sign
  VU sign = ShiftRight<4>(combined);
  sign = ShiftLeft<63>(sign);
  result = Xor(result, BitCast(d, sign));  // Apply sign flip
  return result;
}
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace npsr::HWY_NAMESPACE::trig

HWY_AFTER_NAMESPACE();

#endif  // NPSR_TRIG_HIGH_INL_H_
