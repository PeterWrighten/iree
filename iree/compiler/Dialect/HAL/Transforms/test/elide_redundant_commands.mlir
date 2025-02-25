// RUN: iree-opt -split-input-file -pass-pipeline='func.func(iree-hal-elide-redundant-commands)' %s | FileCheck %s

// Tests that redundant barriers are elided but barriers gaurding ops are not.

// CHECK-LABEL: @elideRedundantBarriers
// CHECK-SAME: (%[[CMD:.+]]: !hal.command_buffer, %[[LAYOUT:.+]]: !hal.executable_layout)
func.func @elideRedundantBarriers(%cmd: !hal.command_buffer, %executable_layout: !hal.executable_layout) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c42_i32 = arith.constant 42 : i32
  // CHECK: hal.command_buffer.execution_barrier
  hal.command_buffer.execution_barrier<%cmd : !hal.command_buffer> source("Dispatch|Transfer|CommandRetire") target("CommandIssue|Dispatch|Transfer") flags("None")
  // CHECK-NOT: hal.command_buffer.execution_barrier
  hal.command_buffer.execution_barrier<%cmd : !hal.command_buffer> source("Dispatch|Transfer|CommandRetire") target("CommandIssue|Dispatch|Transfer") flags("None")
  // CHECK: hal.command_buffer.push_constants
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer> layout(%executable_layout : !hal.executable_layout) offset(0) values([%c42_i32]) : i32
  // CHECK: hal.command_buffer.execution_barrier
  hal.command_buffer.execution_barrier<%cmd : !hal.command_buffer> source("Dispatch|Transfer|CommandRetire") target("CommandIssue|Dispatch|Transfer") flags("None")
  // CHECK: return
  return
}

// -----

// CHECK-LABEL: @elidePushConstants
func.func @elidePushConstants(%cmd: !hal.command_buffer, %executable_layout: !hal.executable_layout) {
  // CHECK-DAG: %[[C0:.+]] = arith.constant 0
  %c0 = arith.constant 0 : i32
  // CHECK-DAG: %[[C1:.+]] = arith.constant 1
  %c1 = arith.constant 1 : i32
  // CHECK: hal.command_buffer.push_constants{{.+}} offset(0) values([%[[C0]], %[[C1]]])
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0, %c1]) : i32, i32
  // CHECK-NOT: hal.command_buffer.push_constants
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0, %c1]) : i32, i32
  // CHECK-NOT: hal.command_buffer.push_constants
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0, %c1]) : i32, i32
  // CHECK: return
  return
}

// -----

// CHECK-LABEL: @elidePushConstantsPrefix
func.func @elidePushConstantsPrefix(%cmd: !hal.command_buffer, %executable_layout: !hal.executable_layout) {
  // CHECK-DAG: %[[C0:.+]] = arith.constant 0
  %c0 = arith.constant 0 : i32
  // CHECK-DAG: %[[C1:.+]] = arith.constant 1
  %c1 = arith.constant 1 : i32
  // CHECK: hal.command_buffer.push_constants{{.+}} offset(0) values([%[[C0]]])
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0]) : i32
  // CHECK: hal.command_buffer.push_constants{{.+}} offset(1) values([%[[C1]]])
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0, %c1]) : i32, i32
  // CHECK-NOT: hal.command_buffer.push_constants
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(1)
      values([%c1]) : i32
  // CHECK: return
  return
}

// -----

// CHECK-LABEL: @elidePushConstantsSuffix
func.func @elidePushConstantsSuffix(%cmd: !hal.command_buffer, %executable_layout: !hal.executable_layout) {
  // CHECK-DAG: %[[C0:.+]] = arith.constant 0
  %c0 = arith.constant 0 : i32
  // CHECK-DAG: %[[C1:.+]] = arith.constant 1
  %c1 = arith.constant 1 : i32
  // CHECK-DAG: %[[C2:.+]] = arith.constant 2
  %c2 = arith.constant 2 : i32
  // CHECK: hal.command_buffer.push_constants{{.+}} offset(0) values([%[[C0]], %[[C1]], %[[C2]]])
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(0)
      values([%c0, %c1, %c2]) : i32, i32, i32
  // CHECK: hal.command_buffer.push_constants{{.+}} offset(1) values([%[[C0]]])
  hal.command_buffer.push_constants<%cmd : !hal.command_buffer>
      layout(%executable_layout : !hal.executable_layout)
      offset(1)
      values([%c0, %c2]) : i32, i32
  // CHECK: return
  return
}

// -----

// NOTE: today we just check for complete equality.

// CHECK-LABEL: @elidePushDescriptorSet
// CHECK-SAME: (%[[CMD:.+]]: !hal.command_buffer, %[[LAYOUT:.+]]: !hal.executable_layout, %[[BUFFER0:.+]]: !hal.buffer, %[[BUFFER1:.+]]: !hal.buffer)
func.func @elidePushDescriptorSet(%cmd: !hal.command_buffer, %executable_layout: !hal.executable_layout, %buffer0: !hal.buffer, %buffer1: !hal.buffer) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  // CHECK-DAG: %[[SIZE0:.+]] = arith.constant 100
  %size0 = arith.constant 100 : index
  // CHECK-DAG: %[[SIZE1:.+]] = arith.constant 101
  %size1 = arith.constant 101 : index
  //      CHECK: hal.command_buffer.push_descriptor_set<%[[CMD]] : !hal.command_buffer> layout(%[[LAYOUT]] : !hal.executable_layout)[%c0] bindings([
  // CHECK-NEXT:   %c0 = (%[[BUFFER0]] : !hal.buffer)[%c0, %[[SIZE0]]],
  // CHECK-NEXT:   %c1 = (%[[BUFFER1]] : !hal.buffer)[%c0, %[[SIZE1]]]
  // CHECK-NEXT: ])
  hal.command_buffer.push_descriptor_set<%cmd : !hal.command_buffer> layout(%executable_layout : !hal.executable_layout)[%c0] bindings([
    %c0 = (%buffer0 : !hal.buffer)[%c0, %size0],
    %c1 = (%buffer1 : !hal.buffer)[%c0, %size1]
  ])
  // CHECK-NOT: hal.command_buffer.push_descriptor_set
  hal.command_buffer.push_descriptor_set<%cmd : !hal.command_buffer> layout(%executable_layout : !hal.executable_layout)[%c0] bindings([
    %c0 = (%buffer0 : !hal.buffer)[%c0, %size0],
    %c1 = (%buffer1 : !hal.buffer)[%c0, %size1]
  ])
  // CHECK: return
  return
}
