// RUN: iree-opt -iree-test-mhlo-convert-complex-to-real %s | FileCheck %s

// CHECK-LABEL: @add
func.func @add(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>, %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xf32>, tensor<2xf32>) {
  %2 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %3 = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: [[VAL0:%.+]] = mhlo.add %arg0, %arg2
  // CHECK-DAG: [[VAL1:%.+]] = mhlo.add %arg1, %arg3
  %4 = "mhlo.add"(%2, %3) : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> (tensor<2xcomplex<f32>>)
  %5 = "mhlo.real"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)
  %6 = "mhlo.imag"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: return [[VAL0]], [[VAL1]]
  return %5, %6 : tensor<2xf32>, tensor<2xf32>
}

// CHECK-LABEL: @sub
func.func @sub(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>, %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xf32>, tensor<2xf32>) {
  %2 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %3 = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: [[VAL0:%.+]] = mhlo.subtract %arg0, %arg2
  // CHECK-DAG: [[VAL1:%.+]] = mhlo.subtract %arg1, %arg3
  %4 = "mhlo.subtract"(%2, %3) : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> (tensor<2xcomplex<f32>>)
  %5 = "mhlo.real"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)
  %6 = "mhlo.imag"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: return [[VAL0]], [[VAL1]]
  return %5, %6 : tensor<2xf32>, tensor<2xf32>
}

// CHECK-LABEL: @mul
func.func @mul(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>, %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xf32>, tensor<2xf32>) {
  %2 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %3 = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: %[[VAL0:.+]] = chlo.broadcast_multiply %arg0, %arg2
  // CHECK-DAG: %[[VAL1:.+]] = chlo.broadcast_multiply %arg1, %arg3
  // CHECK-DAG: %[[VAL2:.+]] = mhlo.subtract %[[VAL0]], %[[VAL1]]
  // CHECK-DAG: %[[VAL3:.+]] = chlo.broadcast_multiply %arg0, %arg3
  // CHECK-DAG: %[[VAL4:.+]] = chlo.broadcast_multiply %arg1, %arg2
  // CHECK-DAG: %[[VAL5:.+]] = mhlo.add %[[VAL3]], %[[VAL4]]
  %4 = "mhlo.multiply"(%2, %3) : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> (tensor<2xcomplex<f32>>)
  %5 = "mhlo.real"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)
  %6 = "mhlo.imag"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: return %2, %5 : tensor<2xf32>, tensor<2xf32>
  return %5, %6 : tensor<2xf32>, tensor<2xf32>
}

// CHECK-LABEL: @div
func.func @div(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>, %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xf32>, tensor<2xf32>) {
  %2 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %3 = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: %[[VAL0:.+]] = "mhlo.negate"(%arg3)

  // Compute the numerator's real component:
  //   numerator.real = lhs.real * rhs.real  lhs.imag * rhs.imag
  // CHECK-DAG: %[[VAL1:.+]] = chlo.broadcast_multiply %arg0, %arg2
  // CHECK-DAG: %[[VAL2:.+]] = chlo.broadcast_multiply %arg1, %[[VAL0]]
  // CHECK-DAG: %[[VAL3:.+]] = mhlo.subtract %[[VAL1]], %[[VAL2]]

  // Compute the real valued denominator as rhs * con(rhs):
  //   denominator = rhs.real * rhs.real + rhs.imag * rhs.imag
  // CHECK-DAG: %[[VAL4:.+]] = mhlo.multiply %arg2, %arg2
  // CHECK-DAG: %[[VAL5:.+]] = mhlo.multiply %arg3, %arg3
  // CHECK-DAG: %[[VAL6:.+]] = mhlo.add %[[VAL4]], %[[VAL5]]

  // Compute the numerator's imaginary component:
  //   numerator.imag = lhs.imag * rhs.real - lhs.real * rhs.imag
  // CHECK-DAG: %[[VAL7:.+]] = chlo.broadcast_multiply %arg1, %arg2
  // CHECK-DAG: %[[VAL8:.+]] = chlo.broadcast_multiply %arg0, %[[VAL0]]
  // CHECK-DAG: %[[VAL9:.+]] = mhlo.add %[[VAL8]], %[[VAL7]]

  // Divide the numerator by the real valued denominator.
  // CHECK-DAG: %[[VAL10:.+]] = chlo.broadcast_divide %[[VAL3]], %[[VAL6]]
  // CHECK-DAG: %[[VAL11:.+]] = chlo.broadcast_divide %[[VAL9]], %[[VAL6]]
  %4 = "mhlo.divide"(%2, %3) : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> (tensor<2xcomplex<f32>>)

  %5 = "mhlo.real"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)
  %6 = "mhlo.imag"(%4) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: return %[[VAL10]], %[[VAL11]]
  return %5, %6 : tensor<2xf32>, tensor<2xf32>
}

// CHECK-LABEL: @abs
func.func @abs(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>) -> (tensor<2xf32>) {
  %0 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: %[[VAL0:.+]] = mhlo.multiply %arg0, %arg0
  // CHECK-DAG: %[[VAL1:.+]] = mhlo.multiply %arg1, %arg1
  // CHECK-DAG: %[[VAL2:.+]] = mhlo.add %[[VAL0]], %[[VAL1]]
  // CHECK-DAG: %[[VAL3:.+]] = "mhlo.sqrt"(%[[VAL2]])
  %1 = "mhlo.abs"(%0) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: return %[[VAL3]]
  return %1 : tensor<2xf32>
}

// CHECK-LABEL: @exp
func.func @exp(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>) -> (tensor<2xf32>, tensor<2xf32>) {
  %0 = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)

  // CHECK-DAG: %[[EXP:.+]] = "mhlo.exponential"(%arg0)
  // CHECK-DAG: %[[COS:.+]] = "mhlo.cosine"(%arg1)
  // CHECK-DAG: %[[SIN:.+]] = "mhlo.sine"(%arg1)
  // CHECK-DAG: %[[OUTR:.+]] = mhlo.multiply %[[COS]], %[[EXP]]
  // CHECK-DAG: %[[OUTI:.+]] = mhlo.multiply %[[SIN]], %[[EXP]]
  %1 = "mhlo.exponential"(%0) : (tensor<2xcomplex<f32>>) -> (tensor<2xcomplex<f32>>)

  %2 = "mhlo.real"(%1) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)
  %3 = "mhlo.imag"(%1) : (tensor<2xcomplex<f32>>) -> (tensor<2xf32>)

  // CHECK: %[[OUTR]], %[[OUTI]]
  return %2, %3 : tensor<2xf32>, tensor<2xf32>
}

// CHECK-LABEL: @compare_eq
func.func @compare_eq(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>,
                 %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xi1>) {
  %lhs = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %rhs = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  // CHECK-DAG: %[[OUTR:.+]] = chlo.broadcast_compare %arg0, %arg2 {comparison_direction = #mhlo<"comparison_direction EQ">}
  // CHECK-DAG: %[[OUTI:.+]] = chlo.broadcast_compare %arg1, %arg3 {comparison_direction = #mhlo<"comparison_direction EQ">}
  // CHECK-DAG: %[[OUT:.+]] = mhlo.and %[[OUTR]], %[[OUTI]]
  %0 = "mhlo.compare"(%lhs, %rhs) {comparison_direction = #mhlo<"comparison_direction EQ">} : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> tensor<2xi1>

  // CHECK: return %[[OUT]]
  return %0 : tensor<2xi1>
}

// CHECK-LABEL: @compare_ne
func.func @compare_ne(%arg0 : tensor<2xf32>, %arg1 : tensor<2xf32>,
                 %arg2 : tensor<2xf32>, %arg3 : tensor<2xf32>) -> (tensor<2xi1>) {
  %lhs = "mhlo.complex"(%arg0, %arg1) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  %rhs = "mhlo.complex"(%arg2, %arg3) : (tensor<2xf32>, tensor<2xf32>) -> (tensor<2xcomplex<f32>>)
  // CHECK-DAG: %[[OUTR:.+]] = chlo.broadcast_compare %arg0, %arg2 {comparison_direction = #mhlo<"comparison_direction NE">}
  // CHECK-DAG: %[[OUTI:.+]] = chlo.broadcast_compare %arg1, %arg3 {comparison_direction = #mhlo<"comparison_direction NE">}
  // CHECK-DAG: %[[OUT:.+]] = mhlo.or %[[OUTR]], %[[OUTI]]
  %0 = "mhlo.compare"(%lhs, %rhs) {comparison_direction = #mhlo<"comparison_direction NE">} : (tensor<2xcomplex<f32>>, tensor<2xcomplex<f32>>) -> tensor<2xi1>

  // CHECK: return %[[OUT]]
  return %0 : tensor<2xi1>
}
