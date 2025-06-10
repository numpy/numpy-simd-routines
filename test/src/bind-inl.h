#if defined(NPSR_TEST_SRC_BIND_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TEST_SRC_BIND_INL_H_
#undef NPSR_TEST_SRC_BIND_INL_H_
#else
#define NPSR_TEST_SRC_BIND_INL_H_
#endif

#include "intrins-inl.h"
#include "npsr/npsr.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE::test {

template <typename TVec>
HWY_API TVec And(const TVec a, const TVec b) {
  return hn::And(a, b);
}
template <typename TVec>
HWY_API TVec Or(const TVec a, const TVec b) {
  return hn::Or(a, b);
}
template <typename TVec>
HWY_API TVec Xor(const TVec a, const TVec b) {
  return hn::Xor(a, b);
}
template <typename TVec>
HWY_API TVec Not(const TVec a) {
  return hn::Not(a);
}
template <typename TVec>
HWY_API TVec Neg(const TVec a) {
  return hn::Neg(a);
}

template <typename... TVec>
HWY_ATTR bool Bind(PyObject *m) {
  bool r = true;
  r &= AttachIntrinsic<And<TVec>...>(m, "And");
  r &= AttachIntrinsic<Xor<TVec>...>(m, "Xor");
  r &= AttachIntrinsic<Or<TVec>...>(m, "Or");
  r &= AttachIntrinsic<Not<TVec>...>(m, "Not");
  r &= AttachIntrinsic<Neg<TVec>...>(m, "Neg");
  return r;
}

}  // namespace npsr::HWY_NAMESPACE::test
HWY_AFTER_NAMESPACE();
#endif  // NPSR_TEST_SRC_BIND_INL_H_
