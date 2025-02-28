#!/usr/bin/env python3
## Copyright 2022 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from suites.definitions import common_definitions, iree_definitions
from suites import cmake_rule_generator
import unittest


class CommonRuleFactoryTest(unittest.TestCase):

  def setUp(self):
    self._factory = cmake_rule_generator.CommonRuleFactory("root/models")

  def test_add_model_rule(self):
    model = common_definitions.Model(
        id="1234",
        name="abcd",
        tags=[],
        source_type=common_definitions.ModelSourceType.EXPORTED_TFLITE,
        source_url="https://example.com/xyz.tflite",
        entry_function="main",
        input_types=["1xf32"])

    rule = self._factory.add_model_rule(model)

    self.assertEqual(rule.target_name, "model-1234")
    self.assertEqual(rule.file_path, "root/models/1234_abcd.tflite")

  def test_generate_cmake_rules(self):
    model_1 = common_definitions.Model(
        id="1234",
        name="abcd",
        tags=[],
        source_type=common_definitions.ModelSourceType.EXPORTED_TFLITE,
        source_url="https://example.com/xyz.tflite",
        entry_function="main",
        input_types=["1xf32"])
    model_2 = common_definitions.Model(
        id="5678",
        name="abcd",
        tags=[],
        source_type=common_definitions.ModelSourceType.EXPORTED_TFLITE,
        source_url="https://example.com/xyz.tflite",
        entry_function="main",
        input_types=["1xf32"])
    rule_1 = self._factory.add_model_rule(model_1)
    rule_2 = self._factory.add_model_rule(model_2)

    rules = self._factory.generate_cmake_rules()

    self.assertEqual(len(rules), 2)
    self.assertRegex(rules[0], rule_1.target_name)
    self.assertRegex(rules[1], rule_2.target_name)


class IreeRuleFactoryTest(unittest.TestCase):

  def setUp(self):
    self._factory = cmake_rule_generator.IreeRuleFactory("root/iree")

  def test_add_import_model_rule_import_model(self):
    rule = self._factory.add_import_model_rule(
        model_id="1234",
        model_name="abcd",
        model_source_type=common_definitions.ModelSourceType.EXPORTED_TFLITE,
        model_entry_function="main",
        source_model_rule=cmake_rule_generator.ModelRule(
            target_name="model-1234", file_path="aaa", cmake_rule="bbb"))

    self.assertEqual(rule.target_name, "iree-import-model-1234")
    self.assertEqual(rule.mlir_dialect_type, "tosa")
    self.assertEqual(rule.output_file_path, "root/iree/1234_abcd/abcd.mlir")

  def test_add_import_model_rule_forward_mlir(self):
    model_rule = cmake_rule_generator.ModelRule(
        target_name="model-1234",
        file_path="root/models/1234.mlir",
        cmake_rule="bbb")

    rule = self._factory.add_import_model_rule(
        model_id="1234",
        model_name="abcd",
        model_source_type=common_definitions.ModelSourceType.
        EXPORTED_LINALG_MLIR,
        model_entry_function="main",
        source_model_rule=model_rule)

    self.assertEqual(rule.target_name, model_rule.target_name)
    self.assertEqual(rule.mlir_dialect_type, "linalg")
    self.assertEqual(rule.output_file_path, model_rule.file_path)

  def test_add_compile_module_rule(self):
    model_rule = cmake_rule_generator.IreeModelImportRule(
        target_name="iree-import-model-1234",
        model_id="1234",
        model_name="abcd",
        output_file_path="root/models/1234.mlir",
        mlir_dialect_type="linalg",
        cmake_rule="bbb")
    compile_config = iree_definitions.CompileConfig(
        id="compa",
        tags=["defaults"],
        compile_targets=[
            iree_definitions.CompileTarget(
                target_architecture=common_definitions.DeviceArchitecture.
                X86_64_CASCADELAKE,
                target_platform=common_definitions.DevicePlatform.LINUX_GNU,
                target_backend=iree_definitions.TargetBackend.LLVM_CPU)
        ],
        extra_flags=[])

    rule = self._factory.add_compile_module_rule(compile_config=compile_config,
                                                 model_import_rule=model_rule)

    self.assertEqual(rule.target_name, "iree-module-1234-compa")
    self.assertEqual(rule.output_module_path, "root/iree/1234_abcd/compa.vmfb")

  def test_generate_cmake_rules(self):
    import_rule_1 = self._factory.add_import_model_rule(
        model_id="1234",
        model_name="abcd",
        model_source_type=common_definitions.ModelSourceType.EXPORTED_TFLITE,
        model_entry_function="main",
        source_model_rule=cmake_rule_generator.ModelRule(
            target_name="model-1234", file_path="aaa", cmake_rule="bbb"))
    import_rule_2 = self._factory.add_import_model_rule(
        model_id="5678",
        model_name="efgh",
        model_source_type=common_definitions.ModelSourceType.EXPORTED_TF,
        model_entry_function="main",
        source_model_rule=cmake_rule_generator.ModelRule(
            target_name="model-5678", file_path="ccc", cmake_rule="eee"))
    compile_config = iree_definitions.CompileConfig(
        id="compa",
        tags=["defaults"],
        compile_targets=[
            iree_definitions.CompileTarget(
                target_architecture=common_definitions.DeviceArchitecture.
                X86_64_CASCADELAKE,
                target_platform=common_definitions.DevicePlatform.LINUX_GNU,
                target_backend=iree_definitions.TargetBackend.LLVM_CPU)
        ],
        extra_flags=[])
    compile_rule = self._factory.add_compile_module_rule(
        compile_config=compile_config, model_import_rule=import_rule_1)

    rules = self._factory.generate_cmake_rules()

    self.assertEqual(len(rules), 3)
    self.assertRegex(rules[0], import_rule_1.target_name)
    self.assertRegex(rules[1], import_rule_2.target_name)
    self.assertRegex(rules[2], compile_rule.target_name)


if __name__ == "__main__":
  unittest.main()
