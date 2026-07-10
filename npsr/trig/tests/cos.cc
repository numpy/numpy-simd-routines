// npsr Cos test library: one Operation registered per Highway target;
// npsr_py_load comes from pyext.cc, the MPFR oracle from mpfr.cc.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "npsr/trig/tests/cos.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "numpy_sr/pyext-inl.h"

HWY_BEFORE_NAMESPACE();
namespace npsr::py::HWY_NAMESPACE {
struct CosFn {
  template <class P, class V>
  V operator()(P& p, V v) const {
    return ::npsr::HWY_NAMESPACE::Cos(p, v);
  }
};
static RegisteredOperation reg_cos{MakeUnaryOperation<CosFn>("cos")};
}  // namespace npsr::py::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();
