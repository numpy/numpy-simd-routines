// C ABI of a per-op test library, exported through `npsr_py_load` (pyext.cc).
// Mirrored field-for-field by numpy_sr/__init__.py -- keep the two in sync.
#ifndef NPSR_PY_PYEXT_H_
#define NPSR_PY_PYEXT_H_

#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace npsr::py {

// numpy kind codes.
enum class TypeID : uint8_t { kFloat32 = 4, kFloat64 = 5 };

enum class FuncID : uint8_t {
  kUnary = 1,
};

// FuncData::prec_mask bits, one per npsr::Precise flag; 0 == default profile.
enum PrecBit : uint64_t {
  kPrecLowAccuracy = 1ull << 0,
  kPrecNoLargeArg = 1ull << 1,
  kPrecNoSpecialCase = 1ull << 2,
  kPrecNoExceptions = 1ull << 3,
  kPrecDAZ = 1ull << 4,
  kPrecFTZ = 1ull << 5,
};

template <class Prec>
constexpr uint64_t PrecMask() {
  uint64_t m = 0;
  if (Prec::kLowAccuracy) m |= kPrecLowAccuracy;
  if (Prec::kNoLargeArgument) m |= kPrecNoLargeArg;
  if (Prec::kNoSpecialCases) m |= kPrecNoSpecialCase;
  if (Prec::kNoExceptions) m |= kPrecNoExceptions;
  if (Prec::kDAZ) m |= kPrecDAZ;
  if (Prec::kFTZ) m |= kPrecFTZ;
  return m;
}

struct FuncData {
  uintptr_t ptr;  // -> void(const T*, T*, size_t), contiguous kernel
  uint64_t prec_mask;
  TypeID type_id;
};

// Operation.target_id for the MPFR reference oracle; HWY_* bits are nonzero.
constexpr int64_t kTargetMpfr = 0;

struct Operation {
  const char* name;
  FuncID func_id;
  int64_t target_id;  // HWY_* bit (lower == better), or kTargetMpfr
  int32_t ndata;      // valid rows in `data`
  FuncData data[64];
};

struct OperationTable {
  int32_t noperations;
  const Operation* operations;  // contiguous array inside the registry
};

// Per-.so registry, defined in pyext.cc: weak/inline header definitions would
// unify across the loaded .so and leak one op's table into another's.
class OperationRegistry {
 public:
  static OperationRegistry& Instance();

  void Add(const Operation& op) {
    assert(table_.noperations < kCapacity);
    ops_[table_.noperations++] = op;
  }

  const OperationTable* Table() {
    table_.operations = ops_;
    return &table_;
  }

 private:
  static constexpr int32_t kCapacity = 64;  // ops per library x targets

  Operation ops_[kCapacity] = {};
  OperationTable table_ = {0, nullptr};
};

// Registers on construction; op files define one static per op family,
// re-expanded in every target's namespace by foreach_target.h.
class RegisteredOperation {
 public:
  explicit RegisteredOperation(std::initializer_list<Operation> ops);
};

}  // namespace npsr::py

#endif  // NPSR_PY_PYEXT_H_
