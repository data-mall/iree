# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

load("//build_tools/bazel:build_defs.oss.bzl", "iree_runtime_cc_library")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
    licenses = ["notice"],  # Apache 2.0
)

iree_runtime_cc_library(
    name = "vmvx",
    srcs = [
        "module.c",
    ],
    hdrs = [
        "module.h",
    ],
    defines = [
        "IREE_HAVE_VMVX_MODULE",
    ],
    textual_hdrs = [
        "exports.inl",
    ],
    deps = [
        "//runtime/src/iree/base",
        "//runtime/src/iree/base:tracing",
        "//runtime/src/iree/builtins/ukernel",
        "//runtime/src/iree/vm",
    ],
)
