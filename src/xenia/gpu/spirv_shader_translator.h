/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "third_party/glslang/SPIRV/SpvBuilder.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  enum DescriptorSet : uint32_t {
    // In order of update frequency.
    // Very frequently changed, especially for UI draws, and for models drawn in
    // multiple parts - contains vertex and texture fetch constants.
    kDescriptorSetFetchConstants,
    // Quite frequently changed (for one object drawn multiple times, for
    // instance - may contain projection matrices).
    kDescriptorSetFloatConstantsVertex,
    // Less frequently changed (per-material).
    kDescriptorSetFloatConstantsPixel,
    // Per-material, combined images and samplers.
    kDescriptorSetTexturesPixel,
    // Rarely used at all, but may be changed at an unpredictable rate when
    // vertex textures are used, combined images and samplers.
    kDescriptorSetTexturesVertex,
    // May stay the same across many draws.
    kDescriptorSetSystemConstants,
    // Pretty rarely used and rarely changed - flow control constants.
    kDescriptorSetBoolLoopConstants,
    // Never changed.
    kDescriptorSetSharedMemoryAndEdram,
    kDescriptorSetCount,
  };
  SpirvShaderTranslator(bool supports_clip_distance = true,
                        bool supports_cull_distance = true);

 protected:
  void Reset() override;

  void StartTranslation() override;

  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessLabel(uint32_t cf_index) override;

  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;

  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  // TODO(Triang3l): Depth-only pixel shader.
  bool IsSpirvVertexOrTessEvalShader() const { return is_vertex_shader(); }
  bool IsSpirvVertexShader() const {
    return IsSpirvVertexOrTessEvalShader() &&
           host_vertex_shader_type() == Shader::HostVertexShaderType::kVertex;
  }
  bool IsSpirvTessEvalShader() const {
    return IsSpirvVertexOrTessEvalShader() &&
           host_vertex_shader_type() != Shader::HostVertexShaderType::kVertex;
  }
  bool IsSpirvFragmentShader() const { return is_pixel_shader(); }

  // Must be called before emitting any non-control-flow SPIR-V operations in
  // translator callback to ensure that if the last instruction added was
  // something like OpBranch - in this case, an unreachable block is created.
  void EnsureBuildPointAvailable();

  void StartVertexOrTessEvalShaderBeforeMain();
  void StartVertexOrTessEvalShaderInMain();
  void CompleteVertexOrTessEvalShaderInMain();
  void CompleteVertexOrTessEvalShaderAfterMain(spv::Instruction* entry_point);

  // Updates the current flow control condition (to be called in the beginning
  // of exec and in jumps), closing the previous conditionals if needed.
  // However, if the condition is not different, the instruction-level predicate
  // conditional also won't be closed - this must be checked separately if
  // needed (for example, in jumps).
  void UpdateExecConditionals(ParsedExecInstruction::Type type,
                              uint32_t bool_constant_index, bool condition);
  // Opens or reopens the predicate check conditional for the instruction.
  // Should be called before processing a non-control-flow instruction.
  void UpdateInstructionPredication(bool predicated, bool condition);
  // Closes the instruction-level predicate conditional if it's open, useful if
  // a control flow instruction needs to do some code which needs to respect the
  // current exec conditional, but can't itself be predicated.
  void CloseInstructionPredication();
  // Closes conditionals opened by exec and instructions within them (but not by
  // labels) and updates the state accordingly.
  void CloseExecConditionals();

  bool supports_clip_distance_;
  bool supports_cull_distance_;

  std::unique_ptr<spv::Builder> builder_;

  std::vector<spv::Id> id_vector_temp_;
  std::vector<unsigned int> uint_vector_temp_;

  spv::Id ext_inst_glsl_std_450_;

  spv::Id type_void_;
  spv::Id type_bool_;
  spv::Id type_int_;
  spv::Id type_int4_;
  spv::Id type_uint_;
  spv::Id type_uint3_;
  spv::Id type_uint4_;
  spv::Id type_float_;
  spv::Id type_float2_;
  spv::Id type_float3_;
  spv::Id type_float4_;

  spv::Id const_int_0_;
  spv::Id const_int4_0_;
  spv::Id const_uint_0_;
  spv::Id const_uint4_0_;
  spv::Id const_float_0_;
  spv::Id const_float4_0_;

  spv::Id uniform_bool_loop_constants_;

  // VS as VS only - int.
  spv::Id input_vertex_index_;
  // VS as TES only - int.
  spv::Id input_primitive_id_;

  enum OutputPerVertexMember : unsigned int {
    kOutputPerVertexMemberPosition,
    kOutputPerVertexMemberPointSize,
    kOutputPerVertexMemberClipDistance,
    kOutputPerVertexMemberCullDistance,
    kOutputPerVertexMemberCount,
  };
  spv::Id output_per_vertex_;

  spv::Function* function_main_;
  // bool.
  spv::Id var_main_predicate_;
  // uint4.
  spv::Id var_main_loop_count_;
  // int4.
  spv::Id var_main_address_relative_;
  // int.
  spv::Id var_main_address_absolute_;
  // float4[register_count()].
  spv::Id var_main_registers_;
  // VS only - float3 (special exports).
  spv::Id var_main_point_size_edge_flag_kill_vertex_;
  spv::Block* main_loop_header_;
  spv::Block* main_loop_continue_;
  spv::Block* main_loop_merge_;
  spv::Id main_loop_pc_next_;
  spv::Block* main_switch_header_;
  std::unique_ptr<spv::Instruction> main_switch_op_;
  spv::Block* main_switch_merge_;
  std::vector<spv::Id> main_switch_next_pc_phi_operands_;

  // If the exec bool constant / predicate conditional is open, block after it
  // (not added to the function yet).
  spv::Block* cf_exec_conditional_merge_;
  // If the instruction-level predicate conditional is open, block after it (not
  // added to the function yet).
  spv::Block* cf_instruction_predicate_merge_;
  // When cf_exec_conditional_merge_ is not null:
  // If the current exec conditional is based on a bool constant: the number of
  // the bool constant.
  // If it's based on the predicate value: kCfExecBoolConstantPredicate.
  uint32_t cf_exec_bool_constant_or_predicate_;
  static constexpr uint32_t kCfExecBoolConstantPredicate = UINT32_MAX;
  // When cf_exec_conditional_merge_ is not null, the expected bool constant or
  // predicate value for the current exec conditional.
  bool cf_exec_condition_;
  // When cf_instruction_predicate_merge_ is not null, the expected predicate
  // value for the current or the last instruction.
  bool cf_instruction_predicate_condition_;
  // Whether there was a `setp` in the current exec before the current
  // instruction, thus instruction-level predicate value can be different than
  // the exec-level predicate value, and can't merge two execs with the same
  // predicate condition anymore.
  bool cf_exec_predicate_written_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
