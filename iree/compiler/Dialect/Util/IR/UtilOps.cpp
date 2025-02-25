// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Util/IR/UtilOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SMLoc.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {

//===----------------------------------------------------------------------===//
// Utils
//===----------------------------------------------------------------------===//

Value findValueSizeInList(unsigned index, ValueRange values, ValueRange sizes) {
  assert(values[index].getType().isa<IREE::Util::SizeAwareTypeInterface>() &&
         "must be a size-aware type to get dims");
  unsigned sizeIndex = 0;
  for (unsigned i = 0; i < index; ++i) {
    if (values[i].getType().isa<IREE::Util::SizeAwareTypeInterface>()) {
      ++sizeIndex;
    }
  }
  return sizes[sizeIndex];
}

//===----------------------------------------------------------------------===//
// custom<SymbolVisibility>($sym_visibility)
//===----------------------------------------------------------------------===//
// some.op custom<SymbolVisibility>($sym_visibility) $sym_name
// ->
// some.op @foo
// some.op private @foo

ParseResult parseSymbolVisibility(OpAsmParser &parser,
                                  StringAttr &symVisibilityAttr) {
  StringRef symVisibility;
  parser.parseOptionalKeyword(&symVisibility, {"public", "private", "nested"});
  if (!symVisibility.empty()) {
    symVisibilityAttr = parser.getBuilder().getStringAttr(symVisibility);
  }
  return success();
}

void printSymbolVisibility(OpAsmPrinter &p, Operation *op,
                           StringAttr symVisibilityAttr) {
  if (!symVisibilityAttr) {
    p << "public";
  } else {
    p << symVisibilityAttr.getValue();
  }
}

//===----------------------------------------------------------------------===//
// custom<TypeOrAttr>($type, $attr)
//===----------------------------------------------------------------------===//
// some.op custom<TypeOrAttr>($type, $attr)
// ->
// some.op : i32
// some.op = 42 : i32
// some.op : i32 = 42 : index

ParseResult parseTypeOrAttr(OpAsmParser &parser, TypeAttr &typeAttr,
                            Attribute &attr) {
  if (succeeded(parser.parseOptionalEqual())) {
    if (failed(parser.parseAttribute(attr))) {
      return parser.emitError(parser.getCurrentLocation())
             << "expected attribute";
    }
    typeAttr = TypeAttr::get(attr.getType());
    return success();
  }

  Type type;
  if (failed(parser.parseColonType(type))) {
    return parser.emitError(parser.getCurrentLocation()) << "expected type";
  }
  typeAttr = TypeAttr::get(type);

  if (succeeded(parser.parseOptionalEqual())) {
    if (failed(parser.parseAttribute(attr))) {
      return parser.emitError(parser.getCurrentLocation())
             << "expected attribute";
    }
  }

  return success();
}

void printTypeOrAttr(OpAsmPrinter &p, Operation *op, TypeAttr type,
                     Attribute attr) {
  bool needsSpace = false;
  if (!attr || attr.getType() != type.getValue()) {
    p << ": ";
    p.printAttribute(type);
    needsSpace = true;  // subsequent attr value needs a space separator
  }
  if (attr) {
    if (needsSpace) p << ' ';
    p << "= ";
    p.printAttribute(attr);
  }
}

//===----------------------------------------------------------------------===//
// custom<TypeAlias>($encoding_type, $storage_type)
//===----------------------------------------------------------------------===//
// tensor<4xf32>
// tensor<4xf32> as tensor<2xf64>

ParseResult parseTypeAlias(OpAsmParser &parser, TypeAttr &encodingTypeAttr,
                           Type &storageType) {
  Type encodingType;
  if (failed(parser.parseType(encodingType))) return failure();
  storageType = encodingType;
  if (succeeded(parser.parseOptionalKeyword("as"))) {
    if (failed(parser.parseType(storageType))) return failure();
  }
  encodingTypeAttr = TypeAttr::get(encodingType);
  return success();
}

void printTypeAlias(OpAsmPrinter &p, Operation *op, TypeAttr encodingTypeAttr,
                    Type storageType) {
  if (encodingTypeAttr.getValue() != storageType) {
    p.printType(encodingTypeAttr.getValue());
    p << " as ";
  }
  p.printType(storageType);
}

//===----------------------------------------------------------------------===//
// custom<RangeList>($offsets, $lengths)
//===----------------------------------------------------------------------===//
// [%offset for %length], [%offset for %length], ...

ParseResult parseRangeList(
    OpAsmParser &parser,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &offsets,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &lengths) {
  do {
    OpAsmParser::UnresolvedOperand offset;
    OpAsmParser::UnresolvedOperand length;
    if (failed(parser.parseLSquare()) || failed(parser.parseOperand(offset)) ||
        failed(parser.parseKeyword("for")) ||
        failed(parser.parseOperand(length)) || failed(parser.parseRSquare())) {
      return failure();
    }
    offsets.push_back(offset);
    lengths.push_back(length);
  } while (succeeded(parser.parseOptionalComma()));
  return success();
}

void printRangeList(OpAsmPrinter &p, Operation *op, OperandRange offsets,
                    OperandRange lengths) {
  llvm::interleaveComma(llvm::zip(offsets, lengths), p, [&](auto it) {
    auto offset = std::get<0>(it);
    auto length = std::get<1>(it);
    p << "[";
    p.printOperand(offset);
    p << " for ";
    p.printOperand(length);
    p << "]";
  });
}

//===----------------------------------------------------------------------===//
// custom<SizeAwareType>
//===----------------------------------------------------------------------===//
// type{%size}

ParseResult parseSizeAwareType(OpAsmParser &parser, Type &type,
                               OpAsmParser::UnresolvedOperand &size) {
  if (failed(parser.parseType(type)) || failed(parser.parseLBrace()) ||
      failed(parser.parseOperand(size)) || failed(parser.parseRBrace())) {
    return failure();
  }
  return success();
}

void printSizeAwareType(OpAsmPrinter &p, Operation *op, Type type, Value size) {
  p.printType(type);
  p << "{";
  p.printOperand(size);
  p << "}";
}

//===----------------------------------------------------------------------===//
// custom<SizeAwareTypeList>
//===----------------------------------------------------------------------===//
// type{%size0}, type, type{%size1}

ParseResult parseSizeAwareTypeList(
    OpAsmParser &parser, SmallVectorImpl<Type> &types,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &sizes) {
  do {
    Type type;
    if (failed(parser.parseType(type))) return failure();
    if (type.isa<IREE::Util::SizeAwareTypeInterface>()) {
      OpAsmParser::UnresolvedOperand size;
      if (failed(parser.parseLBrace()) || failed(parser.parseOperand(size)) ||
          failed(parser.parseRBrace())) {
        return failure();
      }
      sizes.push_back(size);
    }
    types.push_back(type);
  } while (succeeded(parser.parseOptionalComma()));
  return success();
}

void printSizeAwareTypeList(OpAsmPrinter &p, Operation *op, TypeRange types,
                            OperandRange sizes) {
  int sizeIndex = 0;
  llvm::interleaveComma(types, p, [&](Type type) {
    p.printType(type);
    if (type.isa<IREE::Util::SizeAwareTypeInterface>()) {
      p << "{";
      p.printOperand(sizes[sizeIndex++]);
      p << "}";
    }
  });
}

ParseResult parseSizeAwareTypeList(
    OpAsmParser &parser, SmallVectorImpl<Type> &types0,
    SmallVectorImpl<Type> &types1,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &sizes) {
  if (failed(parseSizeAwareTypeList(parser, types0, sizes))) return failure();
  types1 = types0;
  return success();
}

void printSizeAwareTypeList(OpAsmPrinter &p, Operation *op, TypeRange types0,
                            TypeRange types1, OperandRange sizes) {
  printSizeAwareTypeList(p, op, types0, sizes);
}

//===----------------------------------------------------------------------===//
// custom<ShapedTiedResult>
//===----------------------------------------------------------------------===//
// type{%dim0, %dim1}
// %arg0 as type{%dim0}

ParseResult parseShapedTiedResult(
    OpAsmParser &parser, Type &resultType,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &resultDims) {
  ArrayAttr tiedOperands;
  return parseShapedTiedResult(parser, resultType, resultDims, tiedOperands);
}

ParseResult parseShapedTiedResult(
    OpAsmParser &parser, Type &resultType,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &resultDims,
    ArrayAttr &tiedOperands) {
  OpAsmParser::UnresolvedOperand tiedResult;
  auto res = parser.parseOptionalOperand(tiedResult);
  int64_t tiedOperandIndex = IREE::Util::TiedOpInterface::kUntiedIndex;
  if (res.hasValue() && succeeded(res.getValue())) {
    tiedOperandIndex = 0;
    if (failed(parser.parseKeyword("as"))) return failure();
  }
  if (failed(parser.parseType(resultType))) return failure();
  if (auto shapedType = resultType.dyn_cast<ShapedType>()) {
    if (!shapedType.hasStaticShape()) {
      SmallVector<OpAsmParser::UnresolvedOperand, 4> dynamicDims;
      if (failed(parser.parseLBrace()) ||
          failed(parser.parseOperandList(dynamicDims,
                                         shapedType.getNumDynamicDims(),
                                         OpAsmParser::Delimiter::None)) ||
          failed(parser.parseRBrace())) {
        return failure();
      }
      resultDims.append(dynamicDims);
    }
  } else if (auto sizedType =
                 resultType.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
    OpAsmParser::UnresolvedOperand size;
    if (failed(parser.parseLBrace()) || failed(parser.parseOperand(size)) ||
        failed(parser.parseRBrace())) {
      return failure();
    }
    resultDims.push_back(size);
  }
  tiedOperands = parser.getBuilder().getIndexArrayAttr({tiedOperandIndex});
  return success();
}

void printShapedTiedResult(OpAsmPrinter &p, Operation *op, Type resultType,
                           ValueRange resultDims) {
  auto tiedOp = cast<IREE::Util::TiedOpInterface>(op);
  auto tiedOperandIndex = tiedOp.getTiedResultOperandIndex(0);
  if (tiedOperandIndex.hasValue()) {
    auto tiedOperand = op->getOperand(tiedOperandIndex.getValue());
    p.printOperand(tiedOperand);
    p << " as ";
  }
  p.printType(resultType);
  if (auto shapedType = resultType.dyn_cast<ShapedType>()) {
    if (!shapedType.hasStaticShape()) {
      if (resultDims.empty()) {
        p << "{<<INVALID>>}";
        return;
      }
      p << "{";
      llvm::interleaveComma(
          resultDims.take_front(shapedType.getNumDynamicDims()), p,
          [&](Value value) { p.printOperand(value); });
      p << "}";
      resultDims = resultDims.drop_front(shapedType.getNumDynamicDims());
    }
  } else if (auto sizedType =
                 resultType.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
    p << "{";
    p.printOperand(resultDims.front());
    p << "}";
    resultDims = resultDims.drop_front(1);
  }
}

void printShapedTiedResult(OpAsmPrinter &p, Operation *op, Type resultType,
                           ValueRange resultDims, ArrayAttr tiedOperands) {
  printShapedTiedResult(p, op, resultType, resultDims);
}

//===----------------------------------------------------------------------===//
// custom<ShapedFunctionType>
//===----------------------------------------------------------------------===//
// (type, type{%dim0, %dim1}, type) -> (type{%dim2}, %operand4)

static ParseResult parseShapedOperandList(
    OpAsmParser &parser, SmallVectorImpl<Type> &types,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &dims) {
  do {
    Type type;
    if (failed(parser.parseType(type))) return failure();
    if (auto shapedType = type.dyn_cast<ShapedType>()) {
      if (!shapedType.hasStaticShape()) {
        SmallVector<OpAsmParser::UnresolvedOperand, 4> dynamicDims;
        if (failed(parser.parseLBrace()) ||
            failed(parser.parseOperandList(dynamicDims,
                                           shapedType.getNumDynamicDims(),
                                           OpAsmParser::Delimiter::None)) ||
            failed(parser.parseRBrace())) {
          return failure();
        }
        dims.append(dynamicDims);
      }
    } else if (auto sizedType =
                   type.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
      OpAsmParser::UnresolvedOperand size;
      if (failed(parser.parseLBrace()) || failed(parser.parseOperand(size)) ||
          failed(parser.parseRBrace())) {
        return failure();
      }
      dims.push_back(size);
    }
    types.push_back(type);
  } while (succeeded(parser.parseOptionalComma()));
  return success();
}

// Finds the operand index in |operands| that |tiedResult| references.
// Returns TiedOpInterface::kUntiedIndex if no operand is found.
static int64_t findTiedOperand(
    OpAsmParser::UnresolvedOperand tiedResult,
    ArrayRef<OpAsmParser::UnresolvedOperand> operands) {
  int64_t operandIndex = IREE::Util::TiedOpInterface::kUntiedIndex;
  for (int64_t i = 0; i < operands.size(); ++i) {
    if (operands[i].name == tiedResult.name &&
        operands[i].number == tiedResult.number) {
      operandIndex = i;
      break;
    }
  }
  return operandIndex;
}

ParseResult parseShapedResultList(
    OpAsmParser &parser, ArrayRef<OpAsmParser::UnresolvedOperand> operands,
    TypeRange operandTypes,
    ArrayRef<OpAsmParser::UnresolvedOperand> operandDims,
    SmallVectorImpl<Type> &resultTypes,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &resultDims,
    ArrayAttr &tiedOperands) {
  SmallVector<int64_t, 4> tiedOperandIndices;
  do {
    OpAsmParser::UnresolvedOperand tiedResult;
    auto res = parser.parseOptionalOperand(tiedResult);
    Type type;
    int64_t tiedOperandIndex = IREE::Util::TiedOpInterface::kUntiedIndex;
    if (res.hasValue() && succeeded(res.getValue())) {
      tiedOperandIndex = findTiedOperand(tiedResult, operands);
      if (tiedOperandIndex == IREE::Util::TiedOpInterface::kUntiedIndex) {
        return parser.emitError(tiedResult.location,
                                "tied operand not found for result reference ")
               << tiedResult.name;
      }
      if (succeeded(parser.parseOptionalKeyword("as"))) {
        // Type _may_ differ from the operand.
        if (failed(parser.parseType(type))) return failure();
      } else {
        // Use the operands type.
        type = operandTypes[tiedOperandIndex];
      }
    } else if (failed(parser.parseType(type))) {
      return failure();
    }
    if (auto shapedType = type.dyn_cast<ShapedType>()) {
      if (!shapedType.hasStaticShape()) {
        SmallVector<OpAsmParser::UnresolvedOperand, 4> dynamicDims;
        if (failed(parser.parseLBrace()) ||
            failed(parser.parseOperandList(dynamicDims,
                                           shapedType.getNumDynamicDims(),
                                           OpAsmParser::Delimiter::None)) ||
            failed(parser.parseRBrace())) {
          return failure();
        }
        resultDims.append(dynamicDims);
      }
    } else if (auto sizedType =
                   type.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
      OpAsmParser::UnresolvedOperand size;
      if (failed(parser.parseLBrace()) || failed(parser.parseOperand(size)) ||
          failed(parser.parseRBrace())) {
        return failure();
      }
      resultDims.push_back(size);
    }
    resultTypes.push_back(type);
    tiedOperandIndices.push_back(tiedOperandIndex);
  } while (succeeded(parser.parseOptionalComma()));
  if (!tiedOperandIndices.empty()) {
    tiedOperands = parser.getBuilder().getIndexArrayAttr(tiedOperandIndices);
  }
  return success();
}

void printShapedResultList(OpAsmPrinter &p, Operation *op, ValueRange operands,
                           TypeRange operandTypes, ValueRange operandDims,
                           TypeRange resultTypes, ValueRange resultDims,
                           ArrayAttr tiedOperands) {
  auto tiedOp = cast<IREE::Util::TiedOpInterface>(op);
  for (unsigned i = 0; i < resultTypes.size(); ++i) {
    auto resultType = resultTypes[i];
    auto tiedOperandIndex = tiedOp.getTiedResultOperandIndex(i);
    bool printType = true;
    if (tiedOperandIndex.hasValue()) {
      auto tiedOperand = op->getOperand(tiedOperandIndex.getValue());
      p.printOperand(tiedOperand);
      if (tiedOperand.getType() != resultType) {
        p << " as ";
      } else {
        // Type elided as it matches the operand.
        printType = false;
      }
    }
    if (printType) {
      p.printType(resultType);
    }
    if (auto shapedType = resultType.dyn_cast<ShapedType>()) {
      if (!shapedType.hasStaticShape()) {
        if (resultDims.empty()) {
          p << "{<<INVALID>>}";
          return;
        }
        p << "{";
        llvm::interleaveComma(
            resultDims.take_front(shapedType.getNumDynamicDims()), p,
            [&](Value value) { p.printOperand(value); });
        p << "}";
        resultDims = resultDims.drop_front(shapedType.getNumDynamicDims());
      }
    } else if (auto sizedType =
                   resultType.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
      p << "{";
      p.printOperand(resultDims.front());
      p << "}";
      resultDims = resultDims.drop_front(1);
    }
    if (i < resultTypes.size() - 1) p << ", ";
  }
}

ParseResult parseShapedFunctionType(
    OpAsmParser &parser, ArrayRef<OpAsmParser::UnresolvedOperand> operands,
    SmallVectorImpl<Type> &operandTypes,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &operandDims,
    SmallVectorImpl<Type> &resultTypes,
    SmallVectorImpl<OpAsmParser::UnresolvedOperand> &resultDims,
    ArrayAttr &tiedOperands) {
  if (failed(parser.parseLParen())) return failure();
  if (failed(parser.parseOptionalRParen())) {
    if (failed(parseShapedOperandList(parser, operandTypes, operandDims)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  }
  if (failed(parser.parseArrow())) return failure();
  if (succeeded(parser.parseOptionalLParen())) {
    if (failed(parseShapedResultList(parser, operands, operandTypes,
                                     operandDims, resultTypes, resultDims,
                                     tiedOperands)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  } else {
    if (failed(parseShapedResultList(parser, operands, operandTypes,
                                     operandDims, resultTypes, resultDims,
                                     tiedOperands))) {
      return failure();
    }
  }
  return success();
}

void printShapedFunctionType(OpAsmPrinter &p, Operation *op,
                             ValueRange operands, TypeRange operandTypes,
                             OperandRange operandDims, TypeRange resultTypes,
                             OperandRange resultDims, ArrayAttr tiedOperands) {
  p << "(";
  llvm::interleaveComma(operandTypes, p, [&](Type type) {
    p.printType(type);
    if (auto shapedType = type.dyn_cast<ShapedType>()) {
      if (!shapedType.hasStaticShape()) {
        if (operandDims.empty()) {
          p << "{<<INVALID>>}";
          return;
        }
        p << "{";
        llvm::interleaveComma(
            operandDims.take_front(shapedType.getNumDynamicDims()), p,
            [&](Value value) { p.printOperand(value); });
        p << "}";
        operandDims = operandDims.drop_front(shapedType.getNumDynamicDims());
      }
    } else if (auto sizedType =
                   type.dyn_cast<IREE::Util::SizeAwareTypeInterface>()) {
      p << "{";
      p.printOperand(operandDims.front());
      p << "}";
      operandDims = operandDims.drop_front(1);
    }
  });
  p << ") -> ";
  if (resultTypes.size() != 1) p << "(";
  printShapedResultList(p, op, operands, operandTypes, operandDims, resultTypes,
                        resultDims, tiedOperands);
  if (resultTypes.size() != 1) p << ")";
}

namespace IREE {
namespace Util {

//===----------------------------------------------------------------------===//
// util.do_not_optimize
//===----------------------------------------------------------------------===//

void DoNotOptimizeOp::build(OpBuilder &builder, OperationState &state,
                            ValueRange operands,
                            ArrayRef<NamedAttribute> attributes) {
  state.addOperands(operands);
  state.addTypes(llvm::to_vector<2>(operands.getTypes()));
  state.addAttributes(attributes);
}

ParseResult DoNotOptimizeOp::parse(OpAsmParser &parser, OperationState &state) {
  SmallVector<OpAsmParser::UnresolvedOperand, 2> args;
  // Operands and results have the same types.
  auto &operandTypes = state.types;

  if (failed(parser.parseLParen()) || failed(parser.parseOperandList(args)) ||
      failed(parser.parseRParen()) ||
      failed(parser.parseOptionalAttrDict(state.attributes)) ||
      failed(parser.parseOptionalColonTypeList(state.types)) ||
      failed(parser.resolveOperands(
          args, operandTypes, parser.getCurrentLocation(), state.operands))) {
    return failure();
  }

  return success();
}

void DoNotOptimizeOp::print(OpAsmPrinter &p) {
  Operation *op = getOperation();
  p << "(";
  p.printOperands(op->getOperands());
  p << ")";
  p.printOptionalAttrDict(op->getAttrs());

  if (op->getNumOperands() != 0) {
    p << " : ";
    interleaveComma(getOperandTypes(), p);
  }
}

LogicalResult DoNotOptimizeOp::verify() {
  Operation *op = getOperation();
  if (op->getNumOperands() != op->getNumResults()) {
    return op->emitOpError()
           << "must have same number of operands and results, but has "
           << op->getNumOperands() << " and " << op->getNumResults()
           << ", respectively";
  }

  for (int i = 0, e = op->getNumOperands(); i < e; ++i) {
    if (op->getOperand(i).getType() != op->getResult(i).getType()) {
      op->emitOpError() << "must have same operand and result types, but they "
                           "differ at index "
                        << i;
    }
  }

  return success();
}

//===----------------------------------------------------------------------===//
// util.unfoldable_constant
//===----------------------------------------------------------------------===//

// Parsing/printing copied from std.constant

ParseResult UnfoldableConstantOp::parse(OpAsmParser &parser,
                                        OperationState &state) {
  Attribute valueAttr;
  if (parser.parseOptionalAttrDict(state.attributes) ||
      parser.parseAttribute(valueAttr, "value", state.attributes))
    return failure();

  // If the attribute is a symbol reference, then we expect a trailing type.
  Type type;
  if (!valueAttr.isa<SymbolRefAttr>())
    type = valueAttr.getType();
  else if (parser.parseColonType(type))
    return failure();

  // Add the attribute type to the list.
  return parser.addTypeToList(type, state.types);
}

void UnfoldableConstantOp::print(OpAsmPrinter &p) {
  Operation *op = getOperation();
  p << " ";
  p.printOptionalAttrDict(op->getAttrs(), /*elidedAttrs=*/{"value"});

  if (op->getAttrs().size() > 1) p << ' ';
  p << value();

  // If the value is a symbol reference, print a trailing type.
  if (value().isa<SymbolRefAttr>()) p << " : " << getType();
}

//===----------------------------------------------------------------------===//
// Numeric ops
//===----------------------------------------------------------------------===//

Optional<std::pair<int64_t, int64_t>>
NumericOptionalNarrowOp::getIntegerRange() {
  if (!min_value() || !max_value()) return {};
  bool signExtend = isSigned();
  // Note: Cannot sign extend 0 bit values.
  int64_t minValue = signExtend && min_value()->getBitWidth() > 0
                         ? min_value()->getSExtValue()
                         : min_value()->getZExtValue();
  int64_t maxValue = signExtend && max_value()->getBitWidth() > 0
                         ? max_value()->getSExtValue()
                         : max_value()->getZExtValue();
  return std::make_pair(minValue, maxValue);
}

//===----------------------------------------------------------------------===//
// Structural ops
//===----------------------------------------------------------------------===//

void InitializerOp::build(OpBuilder &builder, OperationState &result,
                          ArrayRef<NamedAttribute> attrs) {
  result.addAttribute("function_type", TypeAttr::get(FunctionType::get(
                                           builder.getContext(), {}, {})));
  result.addRegion();
  result.attributes.append(attrs.begin(), attrs.end());
}

ParseResult InitializerOp::parse(OpAsmParser &parser, OperationState &result) {
  result.addAttribute("function_type", TypeAttr::get(FunctionType::get(
                                           result.getContext(), {}, {})));
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes)) {
    return failure();
  }
  auto &body = *result.addRegion();
  if (failed(parser.parseRegion(body))) {
    return failure();
  }
  return success();
}

void InitializerOp::print(OpAsmPrinter &p) {
  Operation *op = getOperation();
  p.printOptionalAttrDictWithKeyword(op->getAttrs(),
                                     /*elidedAttrs=*/{"function_type"});
  p << " ";
  p.printRegion(body());
}

Block *InitializerOp::addEntryBlock() {
  assert(empty() && "function already has an entry block");
  auto *entry = new Block();
  push_back(entry);
  return entry;
}

Block *InitializerOp::addBlock() {
  assert(!empty() && "function should at least have an entry block");
  push_back(new Block());
  return &back();
}

//===----------------------------------------------------------------------===//
// Globals
//===----------------------------------------------------------------------===//

// Returns true if the given |accessType| is compatible with the |globalType|.
// For example, this will return true if the global type is a tensor<?xf32>
// and the access is tensor<4xf32>.
static bool isGlobalTypeCompatible(Type globalType, Type accessType) {
  // If one is a shaped type, then they both must be and have compatible
  // shapes.
  if (globalType.isa<ShapedType>() && accessType.isa<ShapedType>()) {
    return succeeded(mlir::verifyCompatibleShape(globalType, accessType));
  }

  if (auto knownType = globalType.dyn_cast<GlobalTypeInterface>()) {
    return knownType.isAccessStorageCompatible(accessType);
  }

  // Otherwise, the types must be the same.
  return globalType == accessType;
}

void GlobalOp::build(OpBuilder &builder, OperationState &result, StringRef name,
                     bool isMutable, Type type,
                     Optional<Attribute> initialValue,
                     ArrayRef<NamedAttribute> attrs) {
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder.getStringAttr(name));
  if (isMutable) {
    result.addAttribute("is_mutable", builder.getUnitAttr());
  }
  if (initialValue.hasValue()) {
    result.addAttribute("initial_value", initialValue.getValue());
  }
  result.addAttribute("type", TypeAttr::get(type));
  result.attributes.append(attrs.begin(), attrs.end());
}

void GlobalOp::build(OpBuilder &builder, OperationState &result, StringRef name,
                     bool isMutable, Type type,
                     ArrayRef<NamedAttribute> attrs) {
  build(builder, result, name, isMutable, type, llvm::None, attrs);
}

LogicalResult GlobalOp::verify() {
  Operation *op = getOperation();
  if (initial_value().hasValue()) {
    // Ensure the value is something we can convert to a const.
    if (!isGlobalTypeCompatible(type(), initial_valueAttr().getType())) {
      return op->emitOpError()
             << "initial value type mismatch; global " << getSymbolName()
             << " is " << type() << " but initial value provided is "
             << initial_valueAttr().getType();
    }
  }
  return success();
}

IREE::Util::GlobalOp GlobalAddressOp::getGlobalOp() {
  return SymbolTable::lookupNearestSymbolFrom<IREE::Util::GlobalOp>(
      getOperation()->getParentOp(), globalAttr());
}

FlatSymbolRefAttr GlobalAddressOp::getGlobalRefAttr() { return globalAttr(); }

void GlobalAddressOp::getAsmResultNames(
    function_ref<void(Value, StringRef)> setNameFn) {
  setNameFn(result(), Twine("ptr_" + global()).str());
}

LogicalResult GlobalAddressOp::verify() {
  Operation *op = getOperation();
  auto globalOp = getGlobalOp();
  if (!globalOp) {
    return op->emitOpError() << "undefined global: " << global();
  }
  return success();
}

void GlobalLoadOp::build(OpBuilder &builder, OperationState &state,
                         GlobalOp globalOp, ArrayRef<NamedAttribute> attrs) {
  state.addTypes({globalOp.type()});
  state.addAttribute("global", SymbolRefAttr::get(globalOp));
  state.attributes.append(attrs.begin(), attrs.end());
}

IREE::Util::GlobalOp GlobalLoadOp::getGlobalOp() {
  return SymbolTable::lookupNearestSymbolFrom<IREE::Util::GlobalOp>(
      getOperation()->getParentOp(), globalAttr());
}

FlatSymbolRefAttr GlobalLoadOp::getGlobalRefAttr() { return globalAttr(); }

bool GlobalLoadOp::isGlobalImmutable() { return !getGlobalOp().is_mutable(); }

void GlobalLoadOp::getAsmResultNames(
    function_ref<void(Value, StringRef)> setNameFn) {
  setNameFn(result(), global());
}

void GlobalLoadOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  // HACK: works around the lack of symbol side effects in mlir by only saying
  // we have a side-effect if the variable we are loading is mutable.
  auto globalOp =
      SymbolTable::lookupNearestSymbolFrom<GlobalOp>(*this, globalAttr());
  assert(globalOp);
  if (globalOp.is_mutable()) {
    effects.emplace_back(MemoryEffects::Read::get());
  }
}

LogicalResult GlobalLoadOp::verify() {
  Operation *op = getOperation();
  auto globalOp = getGlobalOp();
  if (!globalOp) {
    return op->emitOpError() << "undefined global: " << global();
  }
  auto loadType = op->getResult(0).getType();
  if (!isGlobalTypeCompatible(globalOp.type(), loadType)) {
    return op->emitOpError()
           << "global type mismatch; global " << global() << " is "
           << globalOp.type() << " but load is " << loadType;
  }
  return success();
}

LogicalResult GlobalLoadIndirectOp::verify() {
  Operation *op = getOperation();
  auto globalType =
      global().getType().cast<IREE::Util::PtrType>().getTargetType();
  auto loadType = result().getType();
  if (!isGlobalTypeCompatible(globalType, loadType)) {
    return op->emitOpError() << "global type mismatch; global pointer is "
                             << globalType << " but load is " << loadType;
  }
  return success();
}

IREE::Util::GlobalOp GlobalStoreOp::getGlobalOp() {
  return SymbolTable::lookupNearestSymbolFrom<IREE::Util::GlobalOp>(
      getOperation()->getParentOp(), globalAttr());
}

FlatSymbolRefAttr GlobalStoreOp::getGlobalRefAttr() { return globalAttr(); }

LogicalResult GlobalStoreOp::verify() {
  Operation *op = getOperation();
  auto globalOp = getGlobalOp();
  if (!globalOp) {
    return op->emitOpError() << "undefined global: " << global();
  }
  auto storeType = op->getOperand(0).getType();
  if (globalOp.type() != storeType) {
    return op->emitOpError()
           << "global type mismatch; global " << global() << " is "
           << globalOp.type() << " but store is " << storeType;
  }
  if (!globalOp.isMutable()) {
    // Allow stores to immutable globals in initializers.
    if (!op->getParentOfType<InitializerOp>()) {
      return op->emitOpError() << "global " << global()
                               << " is not mutable and cannot be stored to";
    }
  }
  return success();
}

LogicalResult GlobalStoreIndirectOp::verify() {
  Operation *op = getOperation();
  auto globalType =
      global().getType().cast<IREE::Util::PtrType>().getTargetType();
  auto storeType = value().getType();
  if (!isGlobalTypeCompatible(globalType, storeType)) {
    return op->emitOpError() << "global type mismatch; global pointer is "
                             << globalType << " but store is " << storeType;
  }
  return success();
}

//===----------------------------------------------------------------------===//
// Lists
//===----------------------------------------------------------------------===//

static ParseResult parseListTypeGet(OpAsmParser &parser, Type &listType,
                                    Type &elementType) {
  if (failed(parser.parseType(listType))) {
    return parser.emitError(parser.getCurrentLocation(),
                            "expected !util.list<T> type");
  }
  auto listElementType = listType.cast<ListType>().getElementType();
  if (succeeded(parser.parseOptionalArrow())) {
    // Use overridden type - required for variants only.
    if (failed(parser.parseType(elementType))) {
      return parser.emitError(
          parser.getCurrentLocation(),
          "expected an element type when specifying list access types");
    }
    if (!ListType::canImplicitlyCast(listElementType, elementType)) {
      return parser.emitError(
          parser.getCurrentLocation(),
          "list access types must match the same base type as the list element "
          "type (when not variant)");
    }
  } else {
    // Use list element type as the result element type.
    elementType = listElementType;
  }
  return success();
}

static void printListTypeGet(OpAsmPrinter &printer, Operation *, Type listType,
                             Type elementType) {
  printer.printType(listType);
  auto listElementType = listType.cast<ListType>().getElementType();
  if (listElementType != elementType) {
    printer.printArrowTypeList(ArrayRef<Type>{elementType});
  }
}

static ParseResult parseListTypeSet(OpAsmParser &parser, Type &listType,
                                    Type &elementType) {
  Type leadingType;
  if (failed(parser.parseType(leadingType))) {
    return parser.emitError(parser.getCurrentLocation(),
                            "expected element type or !util.list<T> type");
  }
  if (succeeded(parser.parseOptionalArrow())) {
    elementType = leadingType;
    if (failed(parser.parseType(listType)) || !listType.isa<ListType>()) {
      return parser.emitError(parser.getCurrentLocation(),
                              "expected an !util.list<T> type");
    }
  } else {
    if (!leadingType.isa<ListType>()) {
      return parser.emitError(parser.getCurrentLocation(),
                              "expected an !util.list<T> type");
    }
    listType = leadingType;
    elementType = listType.cast<ListType>().getElementType();
  }
  return success();
}

static void printListTypeSet(OpAsmPrinter &printer, Operation *, Type listType,
                             Type elementType) {
  auto listElementType = listType.cast<ListType>().getElementType();
  if (listElementType != elementType) {
    printer.printType(elementType);
    printer.printArrowTypeList(ArrayRef<Type>{listType});
  } else {
    printer.printType(listType);
  }
}

LogicalResult ListGetOp::verify() {
  Operation *op = getOperation();
  auto listType = list().getType().cast<IREE::Util::ListType>();
  auto elementType = listType.getElementType();
  auto resultType = result().getType();
  if (!ListType::canImplicitlyCast(elementType, resultType)) {
    return op->emitError() << "list contains " << elementType
                           << " and cannot be accessed as " << resultType;
  }
  return success();
}

LogicalResult ListSetOp::verify() {
  Operation *op = getOperation();
  auto listType = list().getType().cast<IREE::Util::ListType>();
  auto elementType = listType.getElementType();
  auto valueType = value().getType();
  if (!ListType::canImplicitlyCast(valueType, elementType)) {
    return op->emitError() << "list contains " << elementType
                           << " and cannot be mutated as " << valueType;
  }
  return success();
}

}  // namespace Util
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#define GET_OP_CLASSES
#include "iree/compiler/Dialect/Util/IR/UtilOps.cpp.inc"
