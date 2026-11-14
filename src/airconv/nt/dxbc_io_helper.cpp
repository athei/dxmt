#include "dxbc_io_helper.hpp"
#include "../airconv_error.hpp"
#include "../dxbc_converter.hpp"
#include "air_vertex_puller.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include <algorithm>

namespace dxmt::dxbc {

IOHelper::IOHelper(
    llvm::air::AIRBuilder &air, llvm::Function *function, air::AirType &types, io_binding_map &io_legacy
) :
    air(air),
    ir(air.builder),
    function(function),
    types(types),
    io(io_legacy) {}

llvm::Value *
IOHelper::GetFloat4Splat(float value) {
  return llvm::ConstantVector::get(
      {llvm::ConstantFP::get(air.getContext(), llvm::APFloat{value}),
       llvm::ConstantFP::get(air.getContext(), llvm::APFloat{value}),
       llvm::ConstantFP::get(air.getContext(), llvm::APFloat{value}),
       llvm::ConstantFP::get(air.getContext(), llvm::APFloat{value})}
  );
};

llvm::Value *
IOHelper::ExtendToVec4(llvm::Value *value) {
  auto ty = value->getType();
  if (ty->isVectorTy()) {
    auto vecTy = llvm::cast<llvm::FixedVectorType>(ty);
    if (vecTy->getNumElements() == 4)
      return value;
    if (vecTy->getNumElements() == 3)
      return ir.CreateShuffleVector(value, {0, 1, 2, 2});
    if (vecTy->getNumElements() == 2)
      return ir.CreateShuffleVector(value, {0, 1, 1, 1});
    if (vecTy->getNumElements() == 1)
      return ir.CreateShuffleVector(value, {0, 0, 0, 0});
    return ir.CreateShuffleVector(value, {0, 1, 2, 3});
  } else {
    return ir.CreateVectorSplat(4, value);
  }
}

llvm::Value *
IOHelper::ToDesiredTypeFromIntVec4(llvm::Value *vec4, llvm::Type *desired, uint32_t mask) {
  llvm::Type *scalar = desired->getScalarType();
  llvm::Value *value = vec4;
  if (scalar->isFloatTy()) {
    value = ir.CreateBitCast(value, types._float4);
  } else if (scalar->isHalfTy()) {
    value = ir.CreateBitCast(ir.CreateTrunc(value, types._short4), types._half4);
  } else if (scalar->isIntegerTy(16)) {
    value = ir.CreateTrunc(value, types._short4);
  } else if (!scalar->isIntegerTy(32)) {
    assert(0 && "unhandled desired element type");
  }

  if (auto *vector = llvm::dyn_cast<llvm::FixedVectorType>(desired)) {
    unsigned num_elements = vector->getNumElements();
    llvm::SmallVector<int, 4> indices;
    for (unsigned i = 0; i < num_elements; i++)
      indices.push_back(mask & (1 << i) ? (int)i : llvm::UndefMaskElem);
    return ir.CreateShuffleVector(value, indices);
  }

  assert(mask);
  return ir.CreateExtractElement(value, mask ? (uint64_t)__builtin_ctz(mask) : 0);
};

llvm::Value *
IOHelper::LoadFromArrayAt(llvm::Value *array, llvm::Value *index) {
  auto array_ty = llvm::cast<llvm::ArrayType>( // force line break
      llvm::cast<llvm::PointerType>(array->getType())->getNonOpaquePointerElementType()
  );
  auto ptr = ir.CreateGEP(array_ty, array, {air.getInt(0), index}, "", llvm::isa<llvm::ConstantInt>(index));
  return ir.CreateLoad(array_ty->getElementType(), ptr);
};

llvm::Value *
IOHelper::LoadFromVec4ArrayMasked(llvm::Value *array, llvm::Value *index, uint32_t mask) {
  if (mask == 0b1111) {
    return LoadFromArrayAt(array, index);
  }
  auto array_type = llvm::cast<llvm::PointerType>(array->getType())->getNonOpaquePointerElementType();
  auto vec4_type = llvm::cast<llvm::ArrayType>(array_type)->getArrayElementType();
  auto ele_type = llvm::cast<llvm::VectorType>(vec4_type)->getElementType();
  llvm::Value *value = llvm::ConstantAggregateZero::get(vec4_type);
  for (unsigned i = 0; i < 4; i++) {
    if ((mask & (1 << i)) == 0)
      continue;
    auto component_ptr = ir.CreateGEP(array_type, array, {air.getInt(0), index, air.getInt(i)});
    value = ir.CreateInsertElement(value, ir.CreateLoad(ele_type, component_ptr), uint64_t(i));
  }
  return value;
};

llvm::Error
IOHelper::StoreAtVec4ArrayMasked(llvm::Value *array, llvm::Value *index, llvm::Value *maybe_vec4, uint32_t mask) {
  auto vec4 = ExtendToVec4(maybe_vec4);
  if (mask == 0b1111) {
    auto ptr = ir.CreateInBoundsGEP(
        llvm::cast<llvm::PointerType>(array->getType())->getNonOpaquePointerElementType(), array, {air.getInt(0), index}
    );
    ir.CreateStore(vec4, ptr);
    return llvm::Error::success();
  }
  for (unsigned i = 0; i < 4; i++) {
    if ((mask & (1 << i)) == 0)
      continue;
    auto component_ptr = ir.CreateGEP(
        llvm::cast<llvm::PointerType>(array->getType())->getNonOpaquePointerElementType(), array,
        {air.getInt(0), index, air.getInt(i)}
    );
    ir.CreateStore(ir.CreateExtractElement(vec4, i), component_ptr);
  }
  return llvm::Error::success();
};

llvm::Error
IOHelper::InitializeInput(uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask, bool fix_w_component) {
  llvm::Value *arg = function->getArg(with_fnarg_at);

  auto ToInt32 = [this](llvm::Value *Value) -> llvm::Expected<llvm::Value *> {
    if (Value->getType()->getScalarType() == types._int)
      return Value;
    if (auto Vector = dyn_cast<llvm::FixedVectorType>(Value->getType())) {
      if (Vector->getScalarSizeInBits() == 32) {
        return ir.CreateBitCast(Value, air.getIntTy(Vector->getNumElements()));
      } else if (Vector->getScalarSizeInBits() == 16) {
        return ir.CreateZExt(
            ir.CreateBitCast(Value, llvm::FixedVectorType::get(ir.getInt16Ty(), Vector->getNumElements())),
            air.getIntTy(Vector->getNumElements())
        );
      }
    } else if (Value->getType()->getScalarType() == Value->getType()) {
      if (Value->getType()->getScalarSizeInBits() == 32) {
        return ir.CreateBitCast(Value, air.getIntTy());
      } else if (Value->getType()->getScalarSizeInBits() == 16) {
        return ir.CreateZExt(ir.CreateBitCast(Value, ir.getInt16Ty()), air.getIntTy());
      }
    }
    return llvm::make_error<UnsupportedFeature>("unhandled input type");
  };

  if (arg->getType()->getScalarType()->isIntegerTy()) {
    if (arg->getType()->getScalarType() == types._bool) {
      // front_facing
      return StoreAtVec4ArrayMasked(io.input.ptr_int4, air.getInt(to_reg), ir.CreateSExt(arg, types._int), mask);
    } else {
      auto Int32Value = ToInt32(arg);
      if (auto err = Int32Value.takeError())
        return err;
      return StoreAtVec4ArrayMasked(io.input.ptr_int4, air.getInt(to_reg), *Int32Value, mask);
    }
  } else if (arg->getType()->getScalarType()->isFloatTy()) {
    if (fix_w_component && isa<llvm::FixedVectorType>(arg->getType()) &&
        cast<llvm::FixedVectorType>(arg->getType())->getNumElements() == 4) {
      auto w_component = ir.CreateExtractElement(arg, 3);
      auto rcp_w = ir.CreateFDiv(air.getFloat(1.0f), w_component);
      arg = ir.CreateInsertElement(arg, rcp_w, 3);
    }
    return StoreAtVec4ArrayMasked(io.input.ptr_float4, air.getInt(to_reg), arg, mask);
  } else if (arg->getType()->getScalarType()->isHalfTy()) {
      auto Int32Value = ToInt32(arg);
      if (auto err = Int32Value.takeError())
        return err;
    return StoreAtVec4ArrayMasked(io.input.ptr_int4, air.getInt(to_reg), *Int32Value, mask);
  } else {
    return llvm::make_error<UnsupportedFeature>("TODO: unhandled input type? might be interpolant...");
  }
  // unreachable
  return llvm::Error::success();
};

llvm::Error
IOHelper::InitializeInterpolatedInput(
    uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask, air::Interpolation interpolation, uint32_t sampleidx_at
) {
  llvm::Value *interpolant = function->getArg(with_fnarg_at);
  llvm::Value *interpolated_val = nullptr;
  switch (interpolation) {
  case air::Interpolation::center_perspective:
    interpolated_val = air.CreateInterpolateAtCenter(interpolant, true);
    break;
  case air::Interpolation::center_no_perspective:
    interpolated_val = air.CreateInterpolateAtCenter(interpolant, false);
    break;
  case air::Interpolation::centroid_perspective:
    interpolated_val = air.CreateInterpolateAtCentroid(interpolant, true);
    break;
  case air::Interpolation::centroid_no_perspective:
    interpolated_val = air.CreateInterpolateAtCentroid(interpolant, false);
    break;
  case air::Interpolation::sample_perspective:
    assert(~sampleidx_at);
    interpolated_val = air.CreateInterpolateAtSample(interpolant, function->getArg(sampleidx_at), true);
    break;
  case air::Interpolation::sample_no_perspective:
    assert(~sampleidx_at);
    interpolated_val = air.CreateInterpolateAtSample(interpolant, function->getArg(sampleidx_at), false);
    break;
  case air::Interpolation::flat:
    return llvm::make_error<UnsupportedFeature>("unexpected interpolant mode: flat");
  }
  return StoreAtVec4ArrayMasked(io.input.ptr_float4, air.getInt(to_reg), interpolated_val, mask);
}

llvm::Error
IOHelper::PullVertexInput(
    uint32_t vbuf_table_arg_index, uint32_t to_reg_unused, uint32_t mask, SM50_IA_INPUT_ELEMENT element_info,
    uint32_t slot_mask
) {
  auto &builder = ir;
  llvm::Value *index;             // uint
  if (element_info.step_function) // per_instance
  {
    if (element_info.step_rate) {
      index = builder.CreateAdd(
          io.base_instance_id, builder.CreateUDiv(io.instance_id, builder.getInt32(element_info.step_rate))
      );
    } else {
      // 0 takes special meaning, that the instance data should never be
      // stepped at all
      index = io.base_instance_id;
    }
  } else {
    index = io.vertex_id_with_base;
  }
  if (!io.vertex_buffer_table) {
    io.vertex_buffer_table = builder.CreateBitCast(
        function->getArg(vbuf_table_arg_index),
        types._dxmt_vertex_buffer_entry->getPointerTo((uint32_t)air::AddressSpace::constant)
    );
  }
  unsigned int shift = 32u - element_info.slot;
  unsigned int vertex_buffer_entry_index = element_info.slot ? __builtin_popcount((slot_mask << shift) >> shift) : 0;
  auto vertex_buffer_table = io.vertex_buffer_table;
  auto vertex_buffer_entry = builder.CreateLoad(
      types._dxmt_vertex_buffer_entry,
      builder.CreateConstGEP1_32(types._dxmt_vertex_buffer_entry, vertex_buffer_table, vertex_buffer_entry_index)
  );
  auto base_addr = builder.CreateExtractValue(vertex_buffer_entry, {0});
  auto stride = builder.CreateExtractValue(vertex_buffer_entry, {1});
  auto byte_offset =
      builder.CreateAdd(builder.CreateMul(stride, index), builder.getInt32(element_info.aligned_byte_offset));
  air::VertexPuller puller(air, *function->getParent());
  auto vec4 = puller.PullVec4((air::MTLVertexFormat)element_info.format, base_addr, byte_offset);
  if (vec4->getType() == types._float4) {
    return StoreAtVec4ArrayMasked(io.input.ptr_float4, builder.getInt32(element_info.reg), vec4, mask);
  } else if (vec4->getType() == types._int4) {
    return StoreAtVec4ArrayMasked(io.input.ptr_int4, builder.getInt32(element_info.reg), vec4, mask);
  } else {
    return llvm::make_error<UnsupportedFeature>("unexpected value type from vertex puller");
  }
};

llvm::Error
IOHelper::InitializeVertexIDInput(uint32_t to_reg, uint32_t mask) {
  return StoreAtVec4ArrayMasked(io.input.ptr_int4, air.getInt(to_reg), io.vertex_id, mask);
}

llvm::Error
IOHelper::InitializeInstanceIDInput(uint32_t to_reg, uint32_t mask) {
  return StoreAtVec4ArrayMasked(io.input.ptr_int4, air.getInt(to_reg), io.instance_id, mask);
}

llvm::Error
IOHelper::InitializeInputAttribute(shader::common::InputAttribute value, uint32_t from_fnarg_at) {
  auto arg = function->getArg(from_fnarg_at);
  switch (value) {
  case shader::common::InputAttribute::ThreadId:
    io.thread_id_arg = arg;
    break;
  case shader::common::InputAttribute::ThreadGroupId:
    io.thread_group_id_arg = arg;
    break;
  case shader::common::InputAttribute::ThreadIdInGroup:
    io.thread_id_in_group_arg = arg;
    break;
  case shader::common::InputAttribute::ThreadIdInGroupFlatten:
    io.thread_id_in_group_flat_arg = arg;
    break;
  case shader::common::InputAttribute::CoverageMask:
    io.coverage_mask_arg = arg;
    break;
  case shader::common::InputAttribute::SampleIndex:
    io.sample_index_arg = arg;
    break;
  default:
    return llvm::make_error<UnsupportedFeature>("unhandled input system value");
  }
  return llvm::Error::success();
}

llvm::Error
IOHelper::CreateDepthOutputRegister() {
  if (io.depth_output_reg != nullptr)
    return llvm::make_error<UnsupportedFeature>("oDepth is defined twice");
  io.depth_output_reg = ir.CreateAlloca(types._float);
  return llvm::Error::success();
}

llvm::Error
IOHelper::CreateBiasedDepthOutputRegister(
    uint32_t position_at, uint32_t bias_at, air::DepthArgument depth_argument
) {
  if (auto err = CreateDepthOutputRegister())
    return err;
  auto z = ir.CreateExtractElement(function->getArg(position_at), uint64_t(2));
  auto bias_ptr = ir.CreateBitCast(
      function->getArg(bias_at), types._float4->getPointerTo((uint32_t)air::AddressSpace::constant)
  );
  auto bias_args = ir.CreateLoad(types._float4, bias_ptr);
  auto constant_bias = ir.CreateExtractElement(bias_args, uint64_t(0));
  auto slope_scale = ir.CreateExtractElement(bias_args, uint64_t(1));
  auto clamp = ir.CreateExtractElement(bias_args, uint64_t(2));
  // the depth is linear in screen space, so its derivatives are the slopes of
  // the primitive's plane
  auto max_slope = air.CreateFPBinOp(
      llvm::air::AIRBuilder::fmax, air.CreateFPUnOp(llvm::air::AIRBuilder::fabs, air.CreateDerivative(z, false)),
      air.CreateFPUnOp(llvm::air::AIRBuilder::fabs, air.CreateDerivative(z, true))
  );
  llvm::Value *bias = ir.CreateFAdd(constant_bias, ir.CreateFMul(slope_scale, max_slope));
  auto clamped_above = air.CreateFPBinOp(llvm::air::AIRBuilder::fmin, clamp, bias);
  auto clamped_below = air.CreateFPBinOp(llvm::air::AIRBuilder::fmax, clamp, bias);
  bias = ir.CreateSelect(
      ir.CreateFCmpOGT(clamp, air.getFloat(0.0f)), clamped_above,
      ir.CreateSelect(ir.CreateFCmpOLT(clamp, air.getFloat(0.0f)), clamped_below, bias)
  );
  auto depth = air.CreateFPUnOp(llvm::air::AIRBuilder::saturate, ir.CreateFAdd(z, bias));
  // The saturation can cross the position's depth only where that lies
  // outside [0, 1]; a depth(greater) or depth(less) output must not
  if (depth_argument == air::DepthArgument::greater)
    depth = air.CreateFPBinOp(llvm::air::AIRBuilder::fmax, z, depth, false);
  else if (depth_argument == air::DepthArgument::less)
    depth = air.CreateFPBinOp(llvm::air::AIRBuilder::fmin, z, depth, false);
  ir.CreateStore(depth, io.depth_output_reg);
  return llvm::Error::success();
}

llvm::Error
IOHelper::CreateStencilRefOutputRegister() {
  if (io.stencil_ref_reg != nullptr)
    return llvm::make_error<UnsupportedFeature>("oStencil is defined twice");
  io.stencil_ref_reg = ir.CreateAlloca(types._int);
  return llvm::Error::success();
}

llvm::Error
IOHelper::CreateCoverageMaskOutputRegister() {
  if (io.coverage_mask_reg != nullptr)
    return llvm::make_error<UnsupportedFeature>("oMask is defined twice");
  io.coverage_mask_reg = ir.CreateAlloca(types._int);
  return llvm::Error::success();
}

llvm::Value *
IOHelper::PopulateOutput(uint32_t from_reg, uint32_t mask, llvm::Value *StructRet, uint32_t to_element) {
  auto desired_type = function->getReturnType()->getStructElementType(to_element);
  auto value = ToDesiredTypeFromIntVec4(LoadFromArrayAt(io.output.ptr_int4, air.getInt(from_reg)), desired_type, mask);
  return ir.CreateInsertValue(StructRet, value, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputFixUnorm(uint32_t from_reg, uint32_t mask, llvm::Value *StructRet, uint32_t to_element) {
  auto fvec4 = LoadFromArrayAt(io.output.ptr_float4, air.getInt(from_reg));
  auto fixed = ir.CreateFSub(fvec4, GetFloat4Splat(1.0f / 127500.0f) /* magic delta ?! */);
  return ir.CreateInsertValue(StructRet, fixed, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputSanitizePosition(
    uint32_t from_reg, uint32_t mask, llvm::Value *StructRet, uint32_t to_element
) {
  auto fvec4 = LoadFromArrayAt(io.output.ptr_float4, air.getInt(from_reg));
  auto fixed = air.SanitizePosition(fvec4);
  return ir.CreateInsertValue(StructRet, fixed, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputDepth(llvm::Value *StructRet, uint32_t to_element) {
  auto value = ir.CreateLoad(types._float, ir.CreateConstInBoundsGEP1_32(types._float, io.depth_output_reg, 0));
  return ir.CreateInsertValue(StructRet, value, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputStencilRef(llvm::Value *StructRet, uint32_t to_element) {
  auto value = ir.CreateLoad(types._int, ir.CreateConstInBoundsGEP1_32(types._int, io.stencil_ref_reg, 0));
  return ir.CreateInsertValue(StructRet, value, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputCoverageMask(llvm::Value *StructRet, uint32_t to_element, uint32_t sample_mask) {
  llvm::Value *value = ir.CreateLoad(types._int, ir.CreateConstInBoundsGEP1_32(types._int, io.coverage_mask_reg, 0));
  if (sample_mask != 0xffffffff)
    value = ir.CreateAnd(value, sample_mask);
  return ir.CreateInsertValue(StructRet, value, {to_element});
}

llvm::Value *
IOHelper::PopulateDefaultCoverageMask(llvm::Value *StructRet, uint32_t to_element, uint32_t sample_mask) {
  if (io.coverage_mask_reg)
    return StructRet;
  return ir.CreateInsertValue(StructRet, air.getInt(sample_mask), {to_element});
}

std::pair<llvm::Value *, llvm::Value *>
IOHelper::InitializeStreamOutputVertex(
    const SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *vertex_so, uint32_t stream_output_draw_arg_index
) {
  auto draw_type = types._dxmt_stream_output_draw;
  auto draw = ir.CreateLoad(
      draw_type, ir.CreateBitCast(
                     function->getArg(stream_output_draw_arg_index),
                     draw_type->getPointerTo((uint32_t)air::AddressSpace::constant)
                 )
  );
  // The draw starts at vertex 0 and has no indices, so this counts the output
  // vertices of an instance
  auto output_vertex = ir.CreateSub(io.vertex_id_with_base, io.base_vertex_id);

  auto const one = ir.getInt32(1);
  auto const two = ir.getInt32(2);
  auto const three = ir.getInt32(3);
  // Triangle n of a strip is {n, n + 1 + (n & 1), n + 2 - (n & 1)}, as the
  // geometry pipeline assembles it
  auto strip_triangle_element = [&]() -> llvm::Value * {
    auto triangle = ir.CreateUDiv(output_vertex, three);
    auto corner = ir.CreateSub(output_vertex, ir.CreateMul(triangle, three));
    auto odd = ir.CreateAnd(triangle, one);
    auto offset = ir.CreateSelect(
        ir.CreateICmpEQ(corner, ir.getInt32(0)), ir.getInt32(0),
        ir.CreateSelect(ir.CreateICmpEQ(corner, one), ir.CreateAdd(one, odd), ir.CreateSub(two, odd))
    );
    return ir.CreateAdd(triangle, offset);
  };
  auto line = ir.CreateLShr(output_vertex, one);
  auto line_end = ir.CreateAnd(output_vertex, one);

  // The element of the draw at this output vertex; the vertices that only
  // tell the adjacency are not streamed out
  llvm::Value *element = output_vertex;
  switch (vertex_so->topology) {
  case SM50_STREAM_OUTPUT_TOPOLOGY_LINE_STRIP:
    element = ir.CreateAdd(line, line_end);
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_TRIANGLE_STRIP:
    element = strip_triangle_element();
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_LINE_LIST_ADJ:
    element = ir.CreateAdd(ir.CreateMul(line, ir.getInt32(4)), ir.CreateAdd(one, line_end));
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_LINE_STRIP_ADJ:
    element = ir.CreateAdd(line, ir.CreateAdd(one, line_end));
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_TRIANGLE_LIST_ADJ:
    element = ir.CreateMul(output_vertex, two);
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_TRIANGLE_STRIP_ADJ:
    element = ir.CreateMul(strip_triangle_element(), two);
    break;
  default:
    break;
  }

  // The draw tells whether it has indices and their size, so one function
  // serves the indexed and the other draws of a pipeline. An index past the
  // end of the index buffer is 0.
  {
    auto index_buffer = ir.CreateExtractValue(draw, {1});
    auto index_size = ir.CreateExtractValue(draw, {6});
    auto current = ir.GetInsertBlock();
    auto load_index = llvm::BasicBlock::Create(ir.getContext(), "stream_output_index", current->getParent());
    auto load_index16 = llvm::BasicBlock::Create(ir.getContext(), "stream_output_index16", current->getParent());
    auto load_index32 = llvm::BasicBlock::Create(ir.getContext(), "stream_output_index32", current->getParent());
    auto continuation = llvm::BasicBlock::Create(ir.getContext(), "stream_output_vertex", current->getParent());
    auto no_indices = ir.CreateICmpEQ(index_size, ir.getInt32(0));
    auto without_index = ir.CreateSelect(no_indices, element, ir.getInt32(0));
    ir.CreateCondBr(
        ir.CreateAnd(ir.CreateNot(no_indices), ir.CreateICmpULT(element, ir.CreateExtractValue(draw, {5}))),
        load_index, continuation
    );
    ir.SetInsertPoint(load_index);
    ir.CreateCondBr(ir.CreateICmpEQ(index_size, two), load_index16, load_index32);
    auto load = [&](llvm::Type *index_type) -> llvm::Value * {
      auto indices = ir.CreateBitCast(index_buffer, index_type->getPointerTo((uint32_t)air::AddressSpace::device));
      return ir.CreateZExt(ir.CreateLoad(index_type, ir.CreateGEP(index_type, indices, {element})), types._int);
    };
    ir.SetInsertPoint(load_index16);
    auto index16 = load(ir.getInt16Ty());
    ir.CreateBr(continuation);
    ir.SetInsertPoint(load_index32);
    auto index32 = load(ir.getInt32Ty());
    ir.CreateBr(continuation);
    ir.SetInsertPoint(continuation);
    auto vertex_id = ir.CreatePHI(types._int, 3);
    vertex_id->addIncoming(without_index, current);
    vertex_id->addIncoming(index16, load_index16);
    vertex_id->addIncoming(index32, load_index32);
    io.vertex_id = vertex_id;
  }
  io.base_vertex_id = ir.CreateExtractValue(draw, {4});
  io.vertex_id_with_base = ir.CreateAdd(io.vertex_id, io.base_vertex_id);

  // The instances follow each other in the output
  auto position = ir.CreateAdd(
      ir.CreateMul(ir.CreateZExt(io.instance_id, types._long), ir.CreateZExt(ir.CreateExtractValue(draw, {3}), types._long)),
      ir.CreateZExt(output_vertex, types._long)
  );
  return {draw, position};
}

llvm::Value *
IOHelper::PopulateOutputStreamOutput(
    llvm::Value *StructRet, const SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *vertex_so,
    const std::vector<uint8_t> &output_masks, llvm::Value *stream_output_draw, llvm::Value *output_vertex
) {
  auto output_regs = ir.CreateBitOrPointerCast(io.output.ptr_int4, llvm::PointerType::get(types._int, 0));

  // The vertex limit covers whole primitives that fit into every slot the
  // declaration writes, and is 0 when one of them has no buffer
  auto fits = ir.CreateICmpULT(output_vertex, ir.CreateZExt(ir.CreateExtractValue(stream_output_draw, {2}), types._long));
  auto current = ir.GetInsertBlock();
  auto write_vertex = llvm::BasicBlock::Create(ir.getContext(), "stream_output_write", current->getParent());
  auto continuation = llvm::BasicBlock::Create(ir.getContext(), "stream_output_done", current->getParent());
  ir.CreateCondBr(fits, write_vertex, continuation);
  ir.SetInsertPoint(write_vertex);

  llvm::Value *vertex_ptr[4] = {};
  for (unsigned i = 0; i < vertex_so->num_elements; i++) {
    auto &element = vertex_so->elements[i];
    if (element.reg_id == 0xffffffff || element.output_slot >= 4)
      continue;
    if (!vertex_ptr[element.output_slot]) {
      auto slot = ir.CreateExtractValue(stream_output_draw, {0, element.output_slot});
      auto vertex_offset = ir.CreateMul(output_vertex, ir.getInt64(vertex_so->strides[element.output_slot] / 4));
      vertex_ptr[element.output_slot] = ir.CreateGEP(types._int, slot, {vertex_offset});
    }
    // The declaration's signature can come from other bytecode than the
    // vertex shader; a component the vertex shader does not write is 0
    llvm::Value *value = ir.getInt32(0);
    if (element.reg_id < output_masks.size() && element.component < 4 &&
        (output_masks[element.reg_id] & (1u << element.component))) {
      auto ptr = ir.CreateConstGEP1_32(types._int, output_regs, (unsigned)(element.reg_id * 4 + element.component));
      value = ir.CreateLoad(types._int, ptr);
    }
    auto target_ptr = ir.CreateConstGEP1_32(types._int, vertex_ptr[element.output_slot], element.offset / 4);
    ir.CreateStore(value, target_ptr, true);
  }

  ir.CreateBr(continuation);
  ir.SetInsertPoint(continuation);
  return StructRet;
}

void
IOHelper::PopulateMeshOutputRenderTargetArrayIndex(uint32_t from_reg, uint32_t mask, llvm::Value *primitive_id) {
  auto result = ToDesiredTypeFromIntVec4(LoadFromArrayAt(io.output.ptr_int4, air.getInt(from_reg)), types._int, mask);
  air.CreateSetMeshRenderTargetArrayIndex(primitive_id, result);
}

void
IOHelper::PopulateMeshOutputViewportArrayIndex(uint32_t from_reg, uint32_t mask, llvm::Value *primitive_id) {
  auto result = ToDesiredTypeFromIntVec4(LoadFromArrayAt(io.output.ptr_int4, air.getInt(from_reg)), types._int, mask);
  air.CreateSetMeshViewportArrayIndex(primitive_id, result);
}

void
IOHelper::PopulateMeshOutputPrimitiveID(uint32_t from_reg, uint32_t mask, llvm::Value *primitive_id) {
  auto result = ToDesiredTypeFromIntVec4(LoadFromArrayAt(io.output.ptr_int4, air.getInt(from_reg)), types._int, mask);
  air.CreateSetMeshPrimitiveID(primitive_id, result);
}

void
IOHelper::PopulateMeshOutputPosition(uint32_t from_reg, uint32_t mask, llvm::Value *vertex_id) {
  auto result = LoadFromArrayAt(io.output.ptr_float4, air.getInt(from_reg));
  air.CreateSetMeshPosition(vertex_id, result);
}

void
IOHelper::PopulateMeshOutputVertexData(
    uint32_t from_reg, uint32_t mask, uint32_t idx, llvm::Value *vertex_id, air::MSLScalerOrVectorType desired_type
) {
  auto result = ToDesiredTypeFromIntVec4(
      LoadFromVec4ArrayMasked(io.output.ptr_int4, air.getInt(from_reg), mask),
      air::get_llvm_type(desired_type, air.getContext()), mask
  );
  air.CreateSetMeshVertexData(vertex_id, idx, result);
}

llvm::Error
Prologues::Run(IOHelper &IO) {
  for (auto &fn : functions) {
    if (auto err = fn(IO))
      return err;
  }
  return llvm::Error::success();
}

llvm::Expected<llvm::Value *>
Epilogues::Run(llvm::Type *TyRet, IOHelper &IO) {
  llvm::Value *ret = TyRet->isVoidTy() ? nullptr : llvm::ConstantAggregateZero::get(TyRet);
  for (auto &fn : functions) {
    auto next = fn(ret, IO);
    if (auto err = next.takeError())
      return err;
    ret = next.get();
  }
  return ret;
}

} // namespace dxmt::dxbc
