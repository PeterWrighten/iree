# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

load("//iree:build_defs.oss.bzl", "iree_cmake_extra_content")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
    licenses = ["notice"],  # Apache 2.0
)

iree_cmake_extra_content(
    content = """
if(${IREE_HAL_DRIVER_VMVX})
""",
    inline = True,
)

cc_library(
    name = "registration",
    srcs = ["driver_module.c"],
    hdrs = ["driver_module.h"],
    defines = [
        "IREE_HAL_HAVE_VMVX_DRIVER_MODULE=1",
    ],
    deps = [
        "//iree/base",
        "//iree/hal",
        "//iree/hal/local",
        "//iree/hal/local:task_driver",
        "//iree/hal/local/loaders:vmvx_module_loader",
        "//iree/task:api",
        "//iree/vm",
    ],
)

iree_cmake_extra_content(
    content = """
endif()

if(${IREE_HAL_DRIVER_VMVX_SYNC})
""",
    inline = True,
)

cc_library(
    name = "sync",
    srcs = ["driver_module_sync.c"],
    hdrs = ["driver_module_sync.h"],
    defines = [
        "IREE_HAL_HAVE_VMVX_SYNC_DRIVER_MODULE=1",
    ],
    deps = [
        "//iree/base",
        "//iree/hal",
        "//iree/hal/local",
        "//iree/hal/local:sync_driver",
        "//iree/hal/local/loaders:vmvx_module_loader",
        "//iree/vm",
    ],
)

iree_cmake_extra_content(
    content = """
endif()
""",
    inline = True,
)
