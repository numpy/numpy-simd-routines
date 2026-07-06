#if defined(NPSR_LUT_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef NPSR_LUT_INL_H_
#undef NPSR_LUT_INL_H_
#else
#define NPSR_LUT_INL_H_
#endif

#include <tuple>
#include <limits>

#include "npsr/hwy.h"

HWY_BEFORE_NAMESPACE();

namespace npsr::HWY_NAMESPACE {

/**
 * @brief Optimized Lookup Table.
 *
 * A fixed-size lookup table (kRows x kCols) optimized for SIMD loads.
 * It automatically selects the fastest loading strategy (TableLookup,
 * Interleaved Loads, or Gather) based on the vector architecture and table dimensions.
 *
 * @par Example Usage
 * @code
 * // 1. Create a 2x4 table (2 rows, 4 columns)
 * auto lut = MakeLut<float>(
 * {1.0f, 2.0f, 3.0f, 4.0f},
 * {5.0f, 6.0f, 7.0f, 8.0f}
 * );
 *
 * // 2. Prepare SIMD indices (e.g., select columns 2, 0, 1, 3...)
 * auto indices = Set(d, 2);
 *
 * // 3. Load values: 'r0' gets values from row 0, 'r1' from row 1
 * Vec<D> r0, r1;
 * lut.Load(indices, r0, r1);
 * @endcode
 */
template <typename T, size_t kRows, size_t kCols>
class Lut {
 public:
  static constexpr size_t kLength = kRows * kCols;

  // Implementation details for transposition optimization
  static constexpr size_t kTransposeBy = HWY_LANES(T);
  static constexpr size_t kTransposeTail = kRows % kTransposeBy;
  // Determine at compile-time if transposition optimization is viable
  static constexpr bool kInitTranspose = !HWY_HAVE_SCALABLE && (
    kRows / kTransposeBy > 0 && kCols % kTransposeBy == 0 &&
    (kTransposeBy == 2 || kTransposeBy == 4) // Currently supports 2x or 4x unrolling
  );
  // When kInitTranspose is false kTransposeLength would be 0 (kRows < kTransposeBy),
  // which would produce a zero-size array (MSVC forbid it) — use 1 as a placeholder instead.
  static constexpr size_t kTransposeLength = kInitTranspose
      ? (kRows - kTransposeTail) * kCols
      : 1;

 /**
   * @brief Constructs the table from row arrays.
   *
   * @param rows Variable number of C-arrays, one for each row.
   * Must match kRows count and kCols size.
   */
  template <size_t... ColSizes>
  constexpr Lut(const T (&...rows)[ColSizes]) : row_{}, trans_{} {
    static_assert(sizeof...(rows) == kRows, "Count of input arrays must match kRows.");
    static_assert(((ColSizes == kCols) && ...), "All input arrays must have kCols elements.");

    // Recursively copy data to internal storage
    const auto &t_rows = std::forward_as_tuple(rows...);
    if constexpr (kInitTranspose) {
      InitTranspose_(t_rows);
    }
    InitRow_(t_rows);
  }

  /**
   * @brief Loads values from the table using SIMD indices.
   *
   * Retrieves values from every row in the table corresponding to the column `idx`.
   *
   * @param idx       SIMD vector containing column indices (0 to kCols-1).
   * @param[out] out  Reference to output vectors. You must provide exactly one
   * output vector per row (total kRows).
   */
  template <typename VU, typename... OutV>
  HWY_INLINE void Load(VU idx, OutV &...out) const {
    static_assert(sizeof...(OutV) == kRows, "Must provide one output vector per table row.");
    using namespace hn;
    using TU = TFromV<VU>;
    static_assert(sizeof(TU) == sizeof(T), "Index vector type must match table element type.");

#if !HWY_HAVE_SCALABLE
    // Try optimized transposed load first
    using DU = DFromV<VU>;
    const DU du;
    constexpr size_t kLanes = HWY_LANES(TU);
    // Try optimized transposed load first
    if constexpr (kInitTranspose && kLanes == kTransposeBy) {
      HWY_ALIGN TU s_idx[kLanes];
      Store(ShiftLeft<kTransposeBy/2>(idx), du, s_idx);
      if constexpr (kTransposeBy == 2) {
        LoadTransposeX2_(s_idx, idx, out...);
      }
      else {
        LoadTransposeX4_(s_idx, idx, out...);
      }
    }
#else
    if constexpr (0) {}
#endif
    else {
      // Fallback to standard row loading
      LoadRow_(idx, out...);
    }
  }

 private:

  // Flattens input arrays into the standard linear member `row_`
  template <size_t RowIDX = 0, typename Tuple>
  constexpr void InitRow_(const Tuple& rows) {
    if constexpr (RowIDX < kRows) {
      const auto& row_array = std::get<RowIDX>(rows);
      for (size_t col = 0; col < kCols; ++col) {
        row_[RowIDX * kCols + col] = row_array[col];
      }
      InitRow_<RowIDX + 1>(rows);
    }
  }

#if !HWY_HAVE_SCALABLE
  // Pre-calculates transposed blocks for specific access patterns
  template <size_t RowIDX = 0, typename Tuple>
  constexpr void InitTranspose_(const Tuple& rows) {
    constexpr size_t kTransposeRows = kRows - kTransposeTail;
    if constexpr (RowIDX < kTransposeRows) {
      const auto& row_array = std::get<RowIDX>(rows);
      const size_t block    = RowIDX / kTransposeBy;
      const size_t in_block = RowIDX % kTransposeBy;
      constexpr size_t block_size = kTransposeBy * kCols;
      for (size_t col = 0; col < kCols; ++col) {
        trans_[
          block * block_size +
          col * kTransposeBy +
          in_block
        ] = row_array[col];
      }
      InitTranspose_<RowIDX + 1>(rows);
    }
  }

  // --- Transposed Load Implementation ---
  // 2-wide transposed load optimization
  template <size_t Off = 0, typename TU, typename VU, typename OutV0, typename... OutV>
  HWY_INLINE void LoadTransposeX2_(const TU *trans_idx, const VU &idx,
                                   OutV0& v0, OutV0& v1, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;

    // Load interleaved data
    const OutV0 a0b0 = LoadU(d, trans_ + Off + trans_idx[0]);
    const OutV0 a1b1 = LoadU(d, trans_ + Off + trans_idx[1]);

    // De-interleave into separate vectors
    v0 = ConcatLowerLower(d, a1b1, a0b0);
    v1 = ConcatUpperUpper(d, a1b1, a0b0);

    // Recurse for remaining rows
    if constexpr (sizeof...(OutV) == 1) {
      LoadRow_<Off + kCols*2>(idx, out...);
    }
    else if constexpr (sizeof...(OutV) > 0) {
      LoadTransposeX2_<Off + kCols*2>(trans_idx, idx, out...);
    }
  }

  // 4-wide transposed load optimization
  template <size_t Off = 0, typename TU, typename VU, typename OutV0, typename... OutV>
  HWY_INLINE void LoadTransposeX4_(const TU *trans_idx, const VU &idx,
                                   OutV0& v0, OutV0& v1, OutV0& v2, OutV0& v3, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;

    const OutV0 abcd0 = LoadU(d, trans_ + Off + trans_idx[0]);
    const OutV0 abcd1 = LoadU(d, trans_ + Off + trans_idx[1]);
    const OutV0 abcd2 = LoadU(d, trans_ + Off + trans_idx[2]);
    const OutV0 abcd3 = LoadU(d, trans_ + Off + trans_idx[3]);

    const OutV0 ab01 = InterleaveLower(d, abcd0, abcd1);
    const OutV0 cd01 = InterleaveUpper(d, abcd0, abcd1);
    const OutV0 ab23 = InterleaveLower(d, abcd2, abcd3);
    const OutV0 cd23 = InterleaveUpper(d, abcd2, abcd3);

    v0 = ConcatLowerLower(d, ab23, ab01);
    v1 = ConcatUpperUpper(d, ab23, ab01);
    v2 = ConcatLowerLower(d, cd23, cd01);
    v3 = ConcatUpperUpper(d, cd23, cd01);

    if constexpr (sizeof...(OutV) <= 3 && sizeof...(OutV) > 0) {
      LoadRow_<Off + kCols*4>(idx, out...);
    }
    else if constexpr (sizeof...(OutV) > 0) {
      LoadTransposeX4_<Off + kCols*4>(trans_idx, idx, out...);
    }
  }
#endif

  // --- Row-Major Load Implementation ---

  // Selects the best row loading strategy based on vector size vs table width
  template <size_t Off = 0, typename VU, typename... OutV>
  HWY_INLINE void LoadRow_(const VU& idx, OutV& ...out) const {
#if !HWY_HAVE_SCALABLE
    using namespace hn;
    using DU = DFromV<VU>;
    const DU du;
    using DI = RebindToSigned<DU>;
    using TI = TFromD<DI>;
    const DI di;
    using D = Rebind<T, DU>;
    using M = MFromD<D>;
    const D d;

    constexpr size_t kLanes = HWY_LANES(TI);
    // Strategy 1: Vector size equals table width (Single Table Lookup)
    if constexpr (kLanes == kCols) {
      const auto ind = IndicesFromVec(d, idx);
      LoadX1_<Off>(ind, out...);
    }
    // Strategy 2: Vector size is half table width (Two Table Lookups)
    else if constexpr (kLanes * 2 == kCols) {
      const auto ind = IndicesFromVec(d, idx);
      LoadX2_<Off>(ind, out...);
    }
    // Strategy 3: Vector size is quarter table width (Four Table Lookups)
    else if constexpr (kLanes * 4 == kCols && kLanes < std::numeric_limits<TI>::max()) {
      const VU lut_lim = Set(du, kLanes * 2  - 1);
      const auto ind = IndicesFromVec(d, And(idx, lut_lim));
      // Note: Rebind to signed because native unsigned 'Greater Than' might be missing
      const M hi_mask = RebindMask(d, Gt(BitCast(di, idx), BitCast(di, lut_lim)));
      LoadX4_<Off>(ind, hi_mask, out...);
    }
#else
    if constexpr (0) {}
#endif
    else {
      // Fallback: Use Gather instructions
      LoadGather_<Off>(idx, out...);
    }
  }

  // Implementation: Single Table Lookup
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

  // Implementation: Two Table Lookups
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

  // Implementation: Four Table Lookups
  template <size_t Off = 0, typename VInd, typename HiMask, typename OutV0, typename... OutV>
  HWY_INLINE void LoadX4_(const VInd &ind, const HiMask &hi_mask, OutV0 &out0, OutV &...out) const {
    using namespace hn;
    using D = DFromV<OutV0>;
    const D d;

    constexpr size_t kLanes = kCols / 4;
    const OutV0 lut0 = LoadU(d, row_ + Off);
    const OutV0 lut1 = LoadU(d, row_ + Off + kLanes);
    const OutV0 lut2 = LoadU(d, row_ + Off + kLanes * 2);
    const OutV0 lut3 = LoadU(d, row_ + Off + kLanes * 3);

    OutV0 lo = TwoTablesLookupLanes(d, lut0, lut1, ind);
    OutV0 hi = TwoTablesLookupLanes(d, lut2, lut3, ind);
    out0 = IfThenElse(hi_mask, hi, lo);

    if constexpr (sizeof...(OutV) > 0) {
      LoadX4_<Off + kCols>(ind, hi_mask, out...);
    }
  }

  // Implementation: Gather fallback
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

  HWY_ALIGN T row_[kLength];
  HWY_ALIGN T trans_[kTransposeLength];
};

/**
 * @brief Deduction Guide (C++17/20).
 * Allows `Lut x{row1, row2};` without specifying template arguments.
 */
template <typename T, size_t First, size_t... Rest>
Lut(const T (&first)[First], const T (&...rest)[Rest])
    -> Lut<T, 1 + sizeof...(Rest), First>;

/**
 * @brief Factory function for explicit type specification.
 *
 * Useful in C++17 where partial template deduction isn't available.
 * * @tparam T Explicit element type (e.g., float).
 * @return   Lut<T, ...> with deduced dimensions.
 *
 * @code
 * auto lut = MakeLut<float>(row1, row2);
 * @endcode
 */
template <typename T, size_t First, size_t... Rest>
constexpr auto MakeLut(const T (&first)[First], const T (&...rest)[Rest]) {
  return Lut<T, 1 + sizeof...(Rest), First>{first, rest...};
}

}  // namespace npsr::HWY_NAMESPACE

HWY_AFTER_NAMESPACE();

#endif  // NPSR_LUT_INL_H_
