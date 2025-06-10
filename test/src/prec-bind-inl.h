#if defined(NPSR_TEST_SRC_PREC_BIND_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_TEST_SRC_PREC_BIND_INL_H_
#undef NPSR_TEST_SRC_PREC_BIND_INL_H_
#else
#define NPSR_TEST_SRC_PREC_BIND_INL_H_
#endif

#include "intrins-inl.h"
#include "npsr/npsr.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::HWY_NAMESPACE::test {

template <typename TPrecise, typename... TVec>
HWY_ATTR bool PreciseBind(PyObject *m) {
  bool r = true;
  r &= AttachIntrinsic<Sin<TPrecise, TVec>...>(m, "Sin");
  r &= AttachIntrinsic<Cos<TPrecise, TVec>...>(m, "Cos");
  return r;
}

}  // namespace npsr::HWY_NAMESPACE::test
HWY_AFTER_NAMESPACE();
#endif  // NPSR_TEST_SRC_PREC_BIND_INL_H_
