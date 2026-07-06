// Compiled into every op .so: per-library registry state + npsr_py_load.
#include "numpy_sr/pyext.h"

namespace npsr::py {

OperationRegistry& OperationRegistry::Instance() {
  static OperationRegistry registry;  // constant-initialised
  return registry;
}

RegisteredOperation::RegisteredOperation(std::initializer_list<Operation> ops) {
  for (const Operation& op : ops) OperationRegistry::Instance().Add(op);
}

}  // namespace npsr::py

extern "C" const npsr::py::OperationTable* npsr_py_load(void) {
  return npsr::py::OperationRegistry::Instance().Table();
}
