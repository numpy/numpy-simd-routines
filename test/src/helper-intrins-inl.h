#include "hwy/base.h"
#if defined(NPSR_TEST_HELPER_INTRINS_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TEST_HELPER_INTRINS_INL_H_
#undef NPSR_TEST_HELPER_INTRINS_INL_H_
#else
#define NPSR_TEST_HELPER_INTRINS_INL_H_
#endif

#include "intrins-inl.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE::test {

namespace hn = hwy::HWY_NAMESPACE;
using hn::DFromV;
using hn::RebindToUnsigned;
using hn::TFromV;

template <typename V, typename T = TFromV<V>, typename D = DFromV<V>,
          typename DU = RebindToUnsigned<D>, typename VU = hn::Vec<DU>>
HWY_API VU UlpDistance(const V& a, const V& b) {
  using namespace hn;
  using DI = RebindToSigned<DU>;
  using VI = Vec<DI>;
  using MU = Mask<DU>;
  using TU = MakeUnsigned<T>;
  const DU du;
  const DI di;

  // Early exit for exact equality
  const MU equal = RebindMask(du, Eq(a, b));

  // Handle special values
  const MU a_special = RebindMask(du, Or(IsNaN(a), IsInf(a)));
  const MU b_special = RebindMask(du, Or(IsNaN(b), IsInf(b)));
  const MU any_special = Or(a_special, b_special);

  const VU a_bits = BitCast(du, a);
  const VU b_bits = BitCast(du, b);

  // IEEE 754 to signed integer conversion for ULP arithmetic
  const VU sign_bit = Set(du, TU(1) << (sizeof(T) * 8 - 1));

  auto ieee_to_signed = [&](const VU& bits) -> VU {
    const MU is_negative = Ne(And(bits, sign_bit), Zero(du));
    return IfThenElse(is_negative, Not(bits), Xor(bits, sign_bit));
  };

  const VI a_signed = BitCast(di, ieee_to_signed(a_bits));
  const VI b_signed = BitCast(di, ieee_to_signed(b_bits));
  const VU ulp_diff = BitCast(du, Abs(Sub(a_signed, b_signed)));

  // Return results
  const VU max_dist = Set(du, ~TU(0));
  return IfThenElse(equal, Zero(du),

                    IfThenElse(any_special, max_dist, ulp_diff));
}

template <typename... TVec>
HWY_ATTR bool BindHelpers(PyObject* m) {
  namespace hn = hwy::HWY_NAMESPACE;
  bool r = true;
  r &= AttachIntrinsic<UlpDistance<TVec>...>(m, "UlpDistance");
  return r;
}
}  // namespace npsr::HWY_NAMESPACE::test

HWY_AFTER_NAMESPACE();
#endif  // NPSR_TEST_HELPER_INTRINS_INL_H_
