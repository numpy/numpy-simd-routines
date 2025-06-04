#include "npsr/common.h"

// clang-format off
#if defined(NPSR_UTILS_INL_H_) == defined(HWY_TARGET_TOGGLE) // NOLINT
#ifdef NPSR_UTILS_INL_H_
#undef NPSR_UTILS_INL_H_
#else
#define NPSR_UTILS_INL_H_
#endif

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE {
using namespace hwy;
using namespace hwy::HWY_NAMESPACE;

template <typename T, typename VU, typename D = Rebind<T, DFromV<VU>>, typename V = Vec<D>>
HWY_API V LutX2(const T *lut, VU idx) {
  D d;
  return GatherIndex(d, lut, BitCast(RebindToSigned<D>(), idx));
#if 0
  D d;
  if constexpr(MaxLanes(d) == sizeof(T)) {
    const V lut0 = Load(d, lut);
    const V lut1 = Load(d, lut + sizeof(T));
    return TwoTablesLookupLanes(d, lut0, lut1, IndicesFromVec(d, idx));
  }
  else if constexpr (MaxLanes(d) == 4){
    const V lut0 = Load(d, lut);
    const V lut1 = Load(d, lut + 4);
    const V lut2 = Load(d, lut + sizeof(T));
    const V lut3 = Load(d, lut + 12);
        
    const auto high_mask = Ne(ShiftRight<3>(idx), Zero(u64));
    const auto load_mask = And(idx, Set(u64, 0b111));
        
    const V lut_low = TwoTablesLookupLanes(d, lut0, lut1, IndicesFromVec(d, load_mask));
    const V lut_high = TwoTablesLookupLanes(d, lut2, lut3, IndicesFromVec(d, load_mask));
        
    return IfThenElse(RebindMask(d, high_mask), lut_high, lut_low);
  }
  else{
    return GatherIndex(d, lut, BitCast(s64, idx));
  }
#endif
}
#if 0
template <typename... Args>
class Lut {
public:
  using T = std::tuple_element_t<0, std::tuple<Args...>>;
  constexpr static size_t kSize = sizeof...(Args);
  
  const T *Data() const {
    return array;
  }

#if 1 || HWY_MAX_BYTES == 16
  // Calculate square root if it's a perfect square
  constexpr static size_t kDim = []() {
    for (size_t i = 1; i * i <= kSize; ++i) {
      if (i * i == kSize) return i;
    }
    return size_t(0);
  }();
  
  static_assert(kDim > 0, "Must provide a perfect square number of array");
  constexpr static size_t kRows = kDim;
  constexpr static size_t kCols = kDim;
  
  constexpr Lut(Args... args)
    : Lut(std::make_tuple(args...), std::make_index_sequence<kSize>{}) {
    static_assert(kDim > 0, "Must provide a perfect square number of array");
  }
 
private:
  template<size_t... Is>
  constexpr Lut(std::tuple<Args...> arg_tuple, std::index_sequence<Is...>)
    : array{static_cast<T>(std::get<(Is % kRows) * kCols + (Is / kRows)>(arg_tuple))...} {
  }

#else
public:
  constexpr Lut(Args... args) 
    : array{static_cast<T>(args)...} {
  } 
#endif
  
private:
  HWY_ALIGN T array[kSize];
};

#endif
} // namespace npsr::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif // NPSR_UTILS_INL_H_
