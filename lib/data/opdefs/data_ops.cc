// Copyright 2020 The TensorFlow Runtime Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//===- data_ops.cc --------------------------------------------------------===//
//
// This file implements MLIR operation functions for the data library.
//
//===----------------------------------------------------------------------===//

#include "tfrt/data/opdefs/data_ops.h"

#include "llvm/Support/FormatVariadic.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/TypeUtilities.h"

namespace tfrt {
namespace data {

//===----------------------------------------------------------------------===//
// DataDialect Dialect
//===----------------------------------------------------------------------===//

DataDialect::DataDialect(MLIRContext *context)
    : Dialect(/*name=*/"data", context) {
  allowUnknownTypes();
  allowUnknownOperations();

  addOperations<
#define GET_OP_LIST
#include "tfrt/data/opdefs/data_ops_opdefs.cpp.inc"
      >();
}

static Type GetIteratorType(Builder *builder) {
  return OpaqueType::get(builder->getIdentifier("hex"), "iterator",
                         builder->getContext());
}

//===----------------------------------------------------------------------===//
// TableGen'd op method definitions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// EnumerateIteratorOp
//===----------------------------------------------------------------------===//

static ParseResult parseEnumerateIteratorOp(OpAsmParser &parser,
                                            OperationState &result) {
  SmallVector<OpAsmParser::OperandType, 4> operands;
  if (parser.parseLParen() || parser.parseOperandList(operands) ||
      parser.parseRParen() || parser.parseOptionalAttrDict(result.attributes))
    return failure();

  SmallVector<Type, 4> types;
  // The first operand is the iterator.
  types.push_back(GetIteratorType(&parser.getBuilder()));
  llvm::SMLoc loc = parser.getCurrentLocation();
  if (parser.parseColonTypeList(types) ||
      parser.resolveOperands(operands, types, loc, result.operands))
    return failure();

  // The results have the same types as the operands besides the first
  // operand (the iterator).
  result.addTypes({types.begin() + 1, types.end()});
  return success();
}

static void print(OpAsmPrinter &p, EnumerateIteratorOp op) {}

// Verify that the signature of the functino matches the operands and results.
static LogicalResult verify(EnumerateIteratorOp op) {
  auto module = op.getParentOfType<ModuleOp>();
  auto function = module.lookupSymbol<FuncOp>(op.function());
  if (!function) {
    return op.emitOpError("function refers to an undefined function: ")
           << op.function();
  }

  auto function_type = function.getType();
  auto results_size = op.getResultTypes().size();

  if (function_type.getNumResults() != results_size) {
    return op.emitError(llvm::formatv(
        "requires the number of function results to be equal to the number of "
        "op results. Found {0} and {1}, respectively",
        function_type.getNumResults(), results_size));
  }

  if (function_type.getNumInputs() <= results_size) {
    // TODO(rachelim): Validate that the number of function inputs ==
    // number of function outputs + number of iterator components.
    // Currently, the number of iterator components is unknown.
    return op.emitError(
        llvm::formatv("requires the number of function inputs to be greater "
                      "than the number of function results. Namely, it should "
                      "have N more inputs, where N is the number of components "
                      "of the iterator. Found {0} and {1}, respectively",
                      function_type.getNumInputs(), results_size));
  }

  // Collect all the type lists for the op so that different pairs of type lists
  // can be compared for the compatibility. The op result types, function result
  // types, and final function input types, should all match.
  constexpr int kNumTypeLists = 3;
  const std::array<std::pair<std::string, ArrayRef<Type>>, kNumTypeLists>
      type_lists = {{
          {"op results", op.getResultTypes()},
          {"function results", function_type.getResults()},
          {"final function inputs",
           function_type.getInputs().take_back(results_size)},
      }};
  for (int i = 0; i < kNumTypeLists; ++i) {
    for (int j = i + 1; j < kNumTypeLists; ++j) {
      auto &a = type_lists[i];
      auto &b = type_lists[j];

      for (int idx = 0; idx < results_size; ++idx) {
        auto a_type = a.second[idx];
        auto b_type = b.second[idx];

        if (a_type != b_type) {
          return op.emitError(llvm::formatv(
              "{0} type {1} is incompatible with {2} type {3} at index {4}",
              a.first, a_type, b.first, b_type, idx));
        }
      }
    }
  }

  return success();
}

#define GET_OP_CLASSES
#include "tfrt/data/opdefs/data_ops_opdefs.cpp.inc"

}  // namespace data
}  // namespace tfrt