# Copyright 2020 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Smoke tests for the execution of tool binaries.

load("//build_tools/bazel:iree_lit_test.bzl", "iree_lit_test_suite")
load("//build_tools/bazel:enforce_glob.bzl", "enforce_glob")

package(
    features = ["layering_check"],
    licenses = ["notice"],  # Apache 2.0
)

iree_lit_test_suite(
    name = "lit",
    srcs = enforce_glob(
        [
            "executable_benchmarks.mlir",
            "iree-benchmark-module.mlir",
            "iree-run-mlir.mlir",
            "iree-run-module.mlir",
            "multiple_args.mlir",
            "multiple_exported_functions.mlir",
            "null_values.mlir",
            "repeated_return.mlir",
            "scalars.mlir",
        ],
        include = ["*.mlir"],
    ),
    cfg = "//tools:lit.cfg.py",
    tags = [
        "hostonly",
    ],
    tools = [
        "//tools:iree-benchmark-module",
        "//tools:iree-compile",
        "//tools:iree-run-mlir",
        "//tools:iree-run-module",
        "@llvm-project//lld",
        "@llvm-project//llvm:FileCheck",
    ],
)

iree_lit_test_suite(
    name = "benchmark_flags",
    srcs = ["benchmark_flags.txt"],
    cfg = "//tools:lit.cfg.py",
    tags = [
        "hostonly",
    ],
    tools = [
        "//tools:iree-benchmark-module",
        "//tools:iree-compile",
        "@llvm-project//llvm:FileCheck",
    ],
)
