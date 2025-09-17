#if defined(NPSR_LUT_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_LUT_INL_H_
#undef NPSR_LUT_INL_H_
#else
#define NPSR_LUT_INL_H_
#endif

#include <tuple>

#include "npsr/hwy.h"

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE {

/**
 * @brief SIMD-optimized lookup table implementation
 *
 * This class provides an efficient lookup table.
 * It stores data in both row-major and column-major
 * formats to optimize different access patterns.
 *
 * @tparam T Element type (must match SIMD vector element type)
 * @tparam kRows Number of rows in the lookup table
 * @tparam kCols Number of columns in the lookup table
 *
 * Example usage:
 * @code
 *   // Create a 2x4 lookup table
 *   constexpr Lut lut{{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}};
 *   // Load values using SIMD indices
 *   auto indices = Set(d, 2);  // SIMD vector of indices
 *   Vec<D> out0, out1;
 *   lut.Load(indices, out0, out1);
 * @endcode
 */
template <typename T, size_t kRows, size_t kCols>
class Lut {
 public:
  static constexpr size_t kLength = kRows * kCols;

  /**
   * @brief Construct a lookup table from row arrays
   *
   * @tparam ColSizes Size of each row array (deduced)
   * @param rows Variable number of arrays, each representing a row
   *
   * @note All rows must have exactly kCols elements
   * @note The constructor is constexpr for compile-time initialization
   */
  template <size_t... ColSizes>
  constexpr Lut(const T (&...rows)[ColSizes]) : row_{} {
    // Check that we have the right number of rows
    static_assert(sizeof...(rows) == kRows,
                  "Number of rows doesn't match template parameter");
    // Check that all rows have the same number of columns
    static_assert(((ColSizes == kCols) && ...),
                  "All rows must have the same number of columns");

    // Copy data using recursive template approach
    ToRowMajor_<0>(rows...);
  }

  /**
   * @brief Load values from the LUT using SIMD indices
   *
   * This method performs efficient SIMD lookups by selecting the optimal
   * implementation based on the vector size and LUT dimensions.
   *
   * @tparam VU SIMD vector type for indices
   * @tparam OutV Output SIMD vector types (must match number of rows)
   * @param idx SIMD vector of column indices
   * @param out Output vectors (one per row)
   *
   * @note The number of output vectors must exactly match kRows
   * @note Index values must be in range [0, kCols)
   */
  template <typename VU, typename... OutV>
  HWY_INLINE void Load(VU idx, OutV &...out) const {
    static_assert(sizeof...(OutV) == kRows,
                  "Number of output vectors must match number of rows in LUT");
    using namespace hn;
    using TU = TFromV<VU>;
    static_assert(sizeof(TU) == sizeof(T),
                  "Index type must match LUT element type");
    // Row-major based optimization
    LoadRow_(idx, out...);
  }

 private:
  /// Convert input rows to row-major storage format
  template <size_t RowIDX, size_t... ColSizes>
  constexpr void ToRowMajor_(const T (&...rows)[ColSizes]) {
    if constexpr (RowIDX < kRows) {
      auto row_array = std::get<RowIDX>(std::make_tuple(rows...));
      for (size_t col = 0; col < kCols; ++col) {
        row_[RowIDX * kCols + col] = row_array[col];
      }
      ToRowMajor_<RowIDX + 1>(rows...);
    }
  }

  /// Dispatch to optimal row-load implementation based on vector/LUT size
  template <size_t Off = 0, typename VU, typename... OutV>
  HWY_INLINE void LoadRow_(VU idx, OutV &...out) const {
    using namespace hn;
    using DU = DFromV<VU>;
    const DU du;
    using D = Rebind<T, DU>;
    const D d;

    HWY_LANES_CONSTEXPR size_t kLanes = Lanes(du);
    if HWY_LANES_CONSTEXPR (kLanes == kCols) {
      // Vector size matches table width - use single table lookup
      const auto ind = IndicesFromVec(d, idx);
      LoadX1_<Off>(ind, out...);
    } else if HWY_LANES_CONSTEXPR (kLanes * 2 == kCols) {
      // Vector size is half table width - use two table lookup
      const auto ind = IndicesFromVec(d, idx);
      LoadX2_<Off>(ind, out...);
    } else {
      // Fallback to gather for other configurations
      LoadGather_<Off>(idx, out...);
    }
  }

  // Load using single table lookup (vector size == table width)
  template <size_t Off = 0, typename VInd, typename OutV0, typename... OutV>
  HWY_INLINE void LoadX1_(const VInd &ind, OutV0 &out0, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;

    const OutV0 lut0 = LoadU(d, row_ + Off);
    out0 = TableLookupLanes(lut0, ind);

    if constexpr (sizeof...(OutV) > 0) {
      LoadX1_<Off + kCols>(ind, out...);
    }
  }

  // Load using two table lookups (vector size == table width / 2)
  template <size_t Off = 0, typename VInd, typename OutV0, typename... OutV>
  HWY_INLINE void LoadX2_(const VInd &ind, OutV0 &out0, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;

    constexpr size_t kLanes = kCols / 2;
    const OutV0 lut0 = LoadU(d, row_ + Off);
    const OutV0 lut1 = LoadU(d, row_ + Off + kLanes);
    out0 = TwoTablesLookupLanes(d, lut0, lut1, ind);

    if constexpr (sizeof...(OutV) > 0) {
      LoadX2_<Off + kCols>(ind, out...);
    }
  }

  //  General fallback using gather instructions
  template <size_t Off = 0, typename VU, typename OutV0, typename... OutV>
  HWY_INLINE void LoadGather_(const VU &idx, OutV0 &out0, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;
    out0 = GatherIndex(d, row_ + Off, BitCast(RebindToSigned<D>(), idx));
    if constexpr (sizeof...(OutV) > 0) {
      LoadGather_<Off + kCols>(idx, out...);
    }
  }

  // Row-major
  HWY_ALIGN T row_[kLength];
};

/**
 * @brief Deduction guide for automatic dimension detection
 *
 * Allows constructing a Lut without explicitly specifying dimensions:
 * @code
 *   Lut lut{row0, row1, row2};  // Dimensions deduced from arrays
 * @endcode
 */
template <typename T, size_t First, size_t... Rest>
Lut(const T (&first)[First], const T (&...rest)[Rest])
    -> Lut<T, 1 + sizeof...(Rest), First>;

/**
 * @brief Factory function that requires explicit type specification
 *
 * This approach forces users to specify the type T explicitly while
 * automatically deducing the dimensions from the array arguments.
 *
 * Note: We use MakeLut since partial deduction guides (e.g., Lut<float>{...})
 * require C++20, but this codebase targets C++17.
 *
 * @tparam T Element type (must be explicitly specified)
 * @param first First row array
 * @param rest Additional row arrays
 * @return Lut with deduced dimensions
 *
 * Usage:
 * @code
 *   auto lut = MakeLut<float>(row0, row1, row2);  // T explicit, dimensions
 * deduced
 * @endcode
 */
template <typename T, size_t First, size_t... Rest>
constexpr auto MakeLut(const T (&first)[First], const T (&...rest)[Rest]) {
  return Lut<T, 1 + sizeof...(Rest), First>{first, rest...};
}

}  // namespace npsr::HWY_NAMESPACE

HWY_AFTER_NAMESPACE();

#endif  // NPSR_LUT_INL_H_
