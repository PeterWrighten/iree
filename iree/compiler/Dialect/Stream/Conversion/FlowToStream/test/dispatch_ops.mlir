// RUN: iree-opt -split-input-file -iree-stream-conversion -canonicalize %s | FileCheck %s

// CHECK-LABEL: @dispatch
//  CHECK-SAME: (%[[INPUT:.+]]: !stream.resource<*>, %[[INPUT_SIZE:.+]]: index, %[[DIM1:.+]]: index, %[[DIM3:.+]]: index)
func.func @dispatch(%input: tensor<7x?x24x?xf32>, %dim1: index, %dim3: index) -> tensor<?x?x1024xf32> {
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  //      CHECK: %[[RESULT_SIZE:.+]] = stream.tensor.sizeof tensor<?x?x1024xf32>{%[[DIM1]], %[[DIM3]]}
  //      CHECK: %[[RESULT:.+]] = stream.async.dispatch @ex::@entry[%c1, %c2, %c3](%[[INPUT]]) :
  // CHECK-SAME:     (!stream.resource<*>{%[[INPUT_SIZE]]}) -> !stream.resource<*>{%[[RESULT_SIZE]]}
  %0 = flow.dispatch @ex::@entry[%c1, %c2, %c3](%input) : (tensor<7x?x24x?xf32>{%dim1, %dim3}) -> tensor<?x?x1024xf32>{%dim1, %dim3}
  // return %[[RESULT]], %[[RESULT_SIZE]] : !stream.resource<*>, index
  return %0 : tensor<?x?x1024xf32>
}

// -----

// CHECK-LABEL: @tiedDispatch
//  CHECK-SAME: (%[[INPUT0:.+]]: !stream.resource<*>, %[[INPUT0_SIZE:.+]]: index, %[[INPUT1:.+]]: !stream.resource<*>, %[[INPUT1_SIZE:.+]]: index)
func.func @tiedDispatch(%input0: tensor<i32>, %input1: tensor<2x3xi32>) -> tensor<3x9xi32> {
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  // CHECK: %[[T_SIZE:.+]] = stream.tensor.sizeof tensor<3x9xi32> : index
  // CHECK: %[[T:.+]] = stream.async.dispatch @ex::@entry0[%c1, %c2, %c3](%[[INPUT0]]) : (!stream.resource<*>{%[[INPUT0_SIZE]]}) -> !stream.resource<*>{%[[T_SIZE]]}
  %0 = flow.dispatch @ex::@entry0[%c1, %c2, %c3](%input0) : (tensor<i32>) -> tensor<3x9xi32>
  // CHECK: %[[RESULT:.+]] = stream.async.dispatch @ex::@entry1[%c1, %c2, %c3](%[[INPUT1]], %[[T]]) : (!stream.resource<*>{%[[INPUT1_SIZE]]}, !stream.resource<*>{%[[T_SIZE]]}) -> %[[T]]{%[[T_SIZE]]}
  %1 = flow.dispatch @ex::@entry1[%c1, %c2, %c3](%input1, %0) : (tensor<2x3xi32>, tensor<3x9xi32>) -> %0
  // CHECK: return %[[RESULT]], %[[T_SIZE]] : !stream.resource<*>, index
  return %1 : tensor<3x9xi32>
}
