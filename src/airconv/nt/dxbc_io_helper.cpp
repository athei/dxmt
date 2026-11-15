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
  llvm::Value *fixed = air.SanitizePosition(fvec4);
  // Outside the clip volume, as PopulateOutputClippedPosition
  if (io.stream_output_discard)
    fixed = ir.CreateSelect(
        io.stream_output_discard,
        llvm::ConstantVector::get({air.getFloat(0.0f), air.getFloat(0.0f), air.getFloat(0.0f), air.getFloat(-1.0f)}),
        fixed
    );
  return ir.CreateInsertValue(StructRet, fixed, {to_element});
}

llvm::Value *
IOHelper::PopulateOutputClippedPosition(llvm::Value *StructRet, uint32_t to_element) {
  auto position = llvm::ConstantVector::get(
      {air.getFloat(0.0f), air.getFloat(0.0f), air.getFloat(0.0f), air.getFloat(-1.0f)}
  );
  return ir.CreateInsertValue(StructRet, position, {to_element});
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

  // The output vertex of the instance the vertex is written at
  llvm::Value *written_vertex = output_vertex;
  // A strip whose primitive adds one index: the indices of a primitive, and
  // the output vertices of one
  uint32_t window = 0, vertices = 0;
  switch (vertex_so->topology) {
  case SM50_STREAM_OUTPUT_TOPOLOGY_LINE_STRIP:
    window = 2, vertices = 2;
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_TRIANGLE_STRIP:
    window = 3, vertices = 3;
    break;
  case SM50_STREAM_OUTPUT_TOPOLOGY_LINE_STRIP_ADJ:
    window = 4, vertices = 2;
    break;
  default:
    break;
  }
  if (window) {
    std::tie(element, written_vertex, io.stream_output_discard) =
        InitializeStreamOutputStripCuts(draw, output_vertex, element, window, vertices);
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
    // The vertices of a primitive left out take vertex 0, not the cut index,
    // which lies past the vertices
    if (io.stream_output_discard)
      io.vertex_id = ir.CreateSelect(io.stream_output_discard, ir.getInt32(0), vertex_id);
  }
  io.base_vertex_id = ir.CreateExtractValue(draw, {4});
  io.vertex_id_with_base = ir.CreateAdd(io.vertex_id, io.base_vertex_id);

  // The instances follow each other in the output
  auto position = ir.CreateAdd(
      ir.CreateMul(ir.CreateZExt(io.instance_id, types._long), ir.CreateZExt(ir.CreateExtractValue(draw, {3}), types._long)),
      ir.CreateZExt(written_vertex, types._long)
  );
  return {draw, position};
}

std::tuple<llvm::Value *, llvm::Value *, llvm::Value *>
IOHelper::InitializeStreamOutputStripCuts(
    llvm::Value *draw, llvm::Value *output_vertex, llvm::Value *element, uint32_t window, uint32_t vertices
) {
  auto &context = ir.getContext();
  auto const zero = ir.getInt32(0);
  auto const one = ir.getInt32(1);
  auto index_size = ir.CreateExtractValue(draw, {6});
  auto before = ir.GetInsertBlock();
  auto scan = llvm::BasicBlock::Create(context, "strip_cuts", function);
  auto carry = llvm::BasicBlock::Create(context, "strip_cuts_carry", function);
  auto carry_blocks = llvm::BasicBlock::Create(context, "strip_cuts_carry_blocks", function);
  auto carry_read = llvm::BasicBlock::Create(context, "strip_cuts_carry_read", function);
  auto carried = llvm::BasicBlock::Create(context, "strip_cuts_carried", function);
  auto loop = llvm::BasicBlock::Create(context, "strip_cuts_loop", function);
  auto body = llvm::BasicBlock::Create(context, "strip_cuts_body", function);
  auto load = llvm::BasicBlock::Create(context, "strip_cuts_load", function);
  auto load16 = llvm::BasicBlock::Create(context, "strip_cuts_load16", function);
  auto load32 = llvm::BasicBlock::Create(context, "strip_cuts_load32", function);
  auto next = llvm::BasicBlock::Create(context, "strip_cuts_next", function);
  auto done = llvm::BasicBlock::Create(context, "strip_cuts_done", function);
  auto after = llvm::BasicBlock::Create(context, "strip_cuts_after", function);
  // Without indices there is no cut
  ir.CreateCondBr(ir.CreateICmpNE(index_size, zero), scan, after);

  // The output vertex is corner of the primitive at position primitive, in
  // block, which scans from the block's first position on. The carry of the
  // block is read from the cuts of STRIP_CUTS_TASK (src/dxmt/dxmt_context.hpp).
  // so_draw_prologue counted the primitives of the blocks the scan carried,
  // and those after them when the scan saw no cut, or those of the first
  // block without cuts.
  auto carry_past = llvm::BasicBlock::Create(context, "strip_cuts_carry_past", function);
  ir.SetInsertPoint(scan);
  auto primitive = ir.CreateUDiv(output_vertex, ir.getInt32(vertices));
  auto corner = ir.CreateSub(output_vertex, ir.CreateMul(primitive, ir.getInt32(vertices)));
  auto block = ir.CreateLShr(primitive, ir.getInt32(10));
  auto first = ir.CreateShl(block, ir.getInt32(10));
  auto cuts = ir.CreateExtractValue(draw, {8});
  auto cut_at = [&](unsigned word) {
    return ir.CreateLoad(
        types._int, ir.CreateGEP(types._int, cuts, {ir.CreateAdd(ir.CreateMul(block, ir.getInt32(4)), ir.getInt32(word))})
    );
  };
  ir.CreateCondBr(ir.CreateICmpNE(block, zero), carry, carried);
  ir.SetInsertPoint(carry);
  ir.CreateCondBr(
      ir.CreateICmpNE(cuts, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(cuts->getType()))),
      carry_blocks, carried
  );
  ir.SetInsertPoint(carry_blocks);
  auto carried_blocks = ir.CreateLoad(types._int, cuts);
  ir.CreateCondBr(ir.CreateICmpULT(block, carried_blocks), carry_read, carry_past);
  ir.SetInsertPoint(carry_read);
  auto carry_read_last = cut_at(6);
  auto carry_read_valid = cut_at(7);
  ir.CreateBr(carried);
  // Past the carried blocks, a strip with no cut in them takes the plain
  // numbering: every position before the block is a primitive
  ir.SetInsertPoint(carry_past);
  auto not_plain = ir.CreateICmpEQ(ir.CreateLoad(types._int, ir.CreateConstGEP1_32(types._int, cuts, 2)), zero);
  ir.CreateBr(carried);
  ir.SetInsertPoint(carried);
  auto carry_last = ir.CreatePHI(types._int, 4);
  carry_last->addIncoming(zero, scan);
  carry_last->addIncoming(zero, carry);
  carry_last->addIncoming(zero, carry_past);
  carry_last->addIncoming(carry_read_last, carry_read);
  auto carry_valid = ir.CreatePHI(types._int, 4);
  carry_valid->addIncoming(zero, scan);
  carry_valid->addIncoming(zero, carry);
  carry_valid->addIncoming(first, carry_past);
  carry_valid->addIncoming(carry_read_valid, carry_read);
  // Any other block past those the scan carried is not drawn
  auto past = ir.CreatePHI(ir.getInt1Ty(), 4);
  past->addIncoming(ir.getFalse(), scan);
  past->addIncoming(ir.getTrue(), carry);
  past->addIncoming(not_plain, carry_past);
  past->addIncoming(ir.getFalse(), carry_read);
  auto end = ir.CreateAdd(primitive, ir.getInt32(window));
  ir.CreateBr(loop);

  // Over the positions from the block's first to the primitive's last: the
  // run of indices without a cut, the last cut before the primitive plus 1,
  // and the primitives before it without a cut
  ir.SetInsertPoint(loop);
  auto position = ir.CreatePHI(types._int, 2);
  auto run = ir.CreatePHI(types._int, 2);
  auto last = ir.CreatePHI(types._int, 2);
  auto valid = ir.CreatePHI(types._int, 2);
  position->addIncoming(first, carried);
  run->addIncoming(zero, carried);
  last->addIncoming(zero, carried);
  valid->addIncoming(zero, carried);
  ir.CreateCondBr(ir.CreateICmpULT(position, end), body, done);

  ir.SetInsertPoint(body);
  ir.CreateCondBr(ir.CreateICmpULT(position, ir.CreateExtractValue(draw, {5})), load, next);
  ir.SetInsertPoint(load);
  auto index_buffer = ir.CreateExtractValue(draw, {1});
  ir.CreateCondBr(ir.CreateICmpEQ(index_size, ir.getInt32(2)), load16, load32);
  auto is_cut = [&](llvm::IntegerType *index_type) {
    auto indices = ir.CreateBitCast(index_buffer, index_type->getPointerTo((uint32_t)air::AddressSpace::device));
    auto index = ir.CreateLoad(index_type, ir.CreateGEP(index_type, indices, {position}));
    return ir.CreateICmpEQ(index, llvm::ConstantInt::get(index_type, index_type->getBitMask()));
  };
  ir.SetInsertPoint(load16);
  auto cut16 = is_cut(ir.getInt16Ty());
  ir.CreateBr(next);
  ir.SetInsertPoint(load32);
  auto cut32 = is_cut(ir.getInt32Ty());
  ir.CreateBr(next);

  ir.SetInsertPoint(next);
  auto cut = ir.CreatePHI(ir.getInt1Ty(), 3);
  cut->addIncoming(ir.getFalse(), body);
  cut->addIncoming(cut16, load16);
  cut->addIncoming(cut32, load32);
  auto next_run = ir.CreateSelect(cut, zero, ir.CreateAdd(run, one));
  auto next_last =
      ir.CreateSelect(ir.CreateAnd(cut, ir.CreateICmpULT(position, primitive)), ir.CreateAdd(position, one), last);
  // The primitive ending here starts before this one
  auto counted = ir.CreateAnd(
      ir.CreateICmpUGE(next_run, ir.getInt32(window)), ir.CreateICmpULT(ir.CreateAdd(position, one), end)
  );
  auto next_valid = ir.CreateAdd(valid, ir.CreateZExt(counted, types._int));
  position->addIncoming(ir.CreateAdd(position, one), next);
  run->addIncoming(next_run, next);
  last->addIncoming(next_last, next);
  valid->addIncoming(next_valid, next);
  ir.CreateBr(loop);

  // A triangle starts the winding of its strip at the strip's first
  // position: the last cut before it plus 1, or the draw's first
  ir.SetInsertPoint(done);
  auto strip_first = ir.CreateSelect(ir.CreateICmpUGT(carry_last, last), carry_last, last);
  llvm::Value *cut_element = ir.CreateAdd(primitive, corner);
  if (vertices == 3) {
    auto odd = ir.CreateAnd(ir.CreateSub(primitive, strip_first), one);
    auto offset = ir.CreateSelect(
        ir.CreateICmpEQ(corner, zero), zero,
        ir.CreateSelect(ir.CreateICmpEQ(corner, one), ir.CreateAdd(one, odd), ir.CreateSub(ir.getInt32(2), odd))
    );
    cut_element = ir.CreateAdd(primitive, offset);
  } else if (window == 4) {
    cut_element = ir.CreateAdd(cut_element, one);
  }
  auto cut_written = ir.CreateAdd(
      ir.CreateMul(ir.CreateAdd(carry_valid, valid), ir.getInt32(vertices)), corner
  );
  auto cut_discard = ir.CreateOr(ir.CreateICmpULT(run, ir.getInt32(window)), past);
  // A primitive left out loads the index at its own position, one of the draw
  cut_element = ir.CreateSelect(cut_discard, primitive, cut_element);
  ir.CreateBr(after);

  ir.SetInsertPoint(after);
  auto element_phi = ir.CreatePHI(types._int, 2);
  element_phi->addIncoming(element, before);
  element_phi->addIncoming(cut_element, done);
  auto written_phi = ir.CreatePHI(types._int, 2);
  written_phi->addIncoming(output_vertex, before);
  written_phi->addIncoming(cut_written, done);
  auto discard_phi = ir.CreatePHI(ir.getInt1Ty(), 2);
  discard_phi->addIncoming(ir.getFalse(), before);
  discard_phi->addIncoming(cut_discard, done);
  return {element_phi, written_phi, discard_phi};
}

llvm::Value *
IOHelper::PopulateOutputStreamOutput(
    llvm::Value *StructRet, const SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *vertex_so,
    const std::vector<uint8_t> &output_masks, llvm::Value *stream_output_draw, llvm::Value *output_vertex
) {
  auto output_regs = ir.CreateBitOrPointerCast(io.output.ptr_int4, llvm::PointerType::get(types._int, 0));

  // The vertex limit covers whole primitives that fit into every slot the
  // declaration writes, and is 0 when one of them has no buffer
  llvm::Value *fits =
      ir.CreateICmpULT(output_vertex, ir.CreateZExt(ir.CreateExtractValue(stream_output_draw, {2}), types._long));
  if (io.stream_output_discard)
    fits = ir.CreateAnd(fits, ir.CreateNot(io.stream_output_discard));
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

GeometryStreamOutput
IOHelper::InitializeGeometryStreamOutput(
    const SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA *data, uint32_t record_arg_index, llvm::Value *index,
    uint32_t vertices_per_primitive
) {
  GeometryStreamOutput state = {};
  state.data = data;
  state.vertices_per_primitive = vertices_per_primitive;
  auto record_type = types._dxmt_geometry_stream_output;
  state.record = ir.CreateLoad(
      record_type, ir.CreateBitCast(
                       function->getArg(record_arg_index),
                       record_type->getPointerTo((uint32_t)air::AddressSpace::constant)
                   )
  );
  // The counts of a threadgroup are those of each stream, one after another
  state.index = ir.CreateMul(index, ir.getInt32(std::max(data->streams, 1u)));
  for (unsigned stream = 0; stream < data->streams && stream < 4; stream++) {
    auto &s = state.streams[stream];
    s.strip_vertices = ir.CreateAlloca(types._int);
    ir.CreateStore(ir.getInt32(0), s.strip_vertices);
    s.primitives = ir.CreateAlloca(types._int);
    ir.CreateStore(ir.getInt32(0), s.primitives);
    if (data->pass == SM50_GEOMETRY_STREAM_OUTPUT_WRITE) {
      auto at = ir.CreateAdd(state.index, ir.getInt32(stream));
      auto counts = ir.CreateExtractValue(state.record, {1});
      s.count = ir.CreateLoad(types._int, ir.CreateGEP(types._int, counts, {at}));
      auto firsts = ir.CreateExtractValue(state.record, {2});
      s.first = ir.CreateLoad(types._int, ir.CreateGEP(types._int, firsts, {at}));
      s.previous = ir.CreateAlloca(llvm::ArrayType::get(types._int, 2 * std::max(data->num_elements, 1u)));
    }
  }
  return state;
}

void
IOHelper::EmitGeometryStreamOutput(GeometryStreamOutput &state, uint32_t stream) {
  auto data = state.data;
  // A stream the declaration does not write emits nothing it counts
  if (stream >= data->streams || stream >= 4)
    return;
  auto &s = state.streams[stream];
  auto vertices = state.vertices_per_primitive;
  auto strip_vertices = ir.CreateLoad(types._int, s.strip_vertices);
  ir.CreateStore(ir.CreateAdd(strip_vertices, ir.getInt32(1)), s.strip_vertices);
  llvm::Value *complete = ir.getTrue();
  if (vertices > 1)
    complete = ir.CreateICmpUGE(strip_vertices, ir.getInt32(vertices - 1));
  auto primitive = ir.CreateLoad(types._int, s.primitives);
  ir.CreateStore(ir.CreateAdd(primitive, ir.CreateZExt(complete, types._int)), s.primitives);
  if (data->pass != SM50_GEOMETRY_STREAM_OUTPUT_WRITE)
    return;

  auto in_stream = [&](const SM50_STREAM_OUTPUT_ELEMENT &element) {
    return element.reg_id != 0xffffffff && element.output_slot < 4 && data->slot_streams[element.output_slot] == stream;
  };
  auto output_regs = ir.CreateBitOrPointerCast(io.output.ptr_int4, llvm::PointerType::get(types._int, 0));
  auto previous_type = s.previous->getType()->getNonOpaquePointerElementType();
  auto previous_at = [&](unsigned vertex, unsigned element) {
    return ir.CreateConstInBoundsGEP2_32(previous_type, s.previous, 0, vertex * data->num_elements + element);
  };
  std::vector<llvm::Value *> current(data->num_elements);
  for (unsigned i = 0; i < data->num_elements; i++) {
    auto &element = data->elements[i];
    current[i] = ir.getInt32(0);
    if (in_stream(element) && element.reg_id < io.output_element_count && element.component < 4)
      current[i] = ir.CreateLoad(
          types._int, ir.CreateConstGEP1_32(types._int, output_regs, element.reg_id * 4 + element.component)
      );
  }

  // The primitive is written as a list; triangle n of a strip is
  // {n, n + 1 + (n & 1), n + 2 - (n & 1)}, as the vertex path writes it. A
  // pass that emits more than the count pass counted, as one with side
  // effects can, keeps to the room the count gave it.
  auto position = ir.CreateAdd(s.first, primitive);
  auto limit = ir.CreateExtractValue(state.record, {3, stream});
  auto write = llvm::BasicBlock::Create(ir.getContext(), "stream_output_write", function);
  auto done = llvm::BasicBlock::Create(ir.getContext(), "stream_output_done", function);
  ir.CreateCondBr(
      ir.CreateAnd(
          complete, ir.CreateAnd(ir.CreateICmpULT(position, limit), ir.CreateICmpULT(primitive, s.count))
      ),
      write, done
  );
  ir.SetInsertPoint(write);
  auto odd = ir.CreateICmpNE(ir.CreateAnd(strip_vertices, ir.getInt32(1)), ir.getInt32(0));
  auto first_vertex = ir.CreateMul(ir.CreateZExt(position, types._long), ir.getInt64(vertices));
  for (unsigned v = 0; v < vertices; v++) {
    for (unsigned i = 0; i < data->num_elements; i++) {
      auto &element = data->elements[i];
      if (!in_stream(element))
        continue;
      llvm::Value *value = current[i];
      if (vertices == 2 && v == 0) {
        value = ir.CreateLoad(types._int, previous_at(1, i));
      } else if (vertices == 3 && v == 0) {
        value = ir.CreateLoad(types._int, previous_at(0, i));
      } else if (vertices == 3 && v == 1) {
        value = ir.CreateSelect(odd, current[i], ir.CreateLoad(types._int, previous_at(1, i)));
      } else if (vertices == 3 && v == 2) {
        value = ir.CreateSelect(odd, ir.CreateLoad(types._int, previous_at(1, i)), current[i]);
      }
      auto slot = ir.CreateExtractValue(state.record, {0, element.output_slot});
      auto offset = ir.CreateAdd(
          ir.CreateMul(ir.CreateAdd(first_vertex, ir.getInt64(v)), ir.getInt64(data->strides[element.output_slot] / 4)),
          ir.getInt64(element.offset / 4)
      );
      ir.CreateStore(value, ir.CreateGEP(types._int, slot, {offset}), true);
    }
  }
  ir.CreateBr(done);
  ir.SetInsertPoint(done);

  for (unsigned i = 0; i < data->num_elements && vertices > 1; i++) {
    if (!in_stream(data->elements[i]))
      continue;
    if (vertices == 3)
      ir.CreateStore(ir.CreateLoad(types._int, previous_at(1, i)), previous_at(0, i));
    ir.CreateStore(current[i], previous_at(1, i));
  }
}

void
IOHelper::CutGeometryStreamOutput(GeometryStreamOutput &state, uint32_t stream) {
  if (stream < state.data->streams && stream < 4)
    ir.CreateStore(ir.getInt32(0), state.streams[stream].strip_vertices);
}

void
IOHelper::FinishGeometryStreamOutput(GeometryStreamOutput &state) {
  if (state.data->pass != SM50_GEOMETRY_STREAM_OUTPUT_COUNT)
    return;
  auto counts = ir.CreateExtractValue(state.record, {1});
  for (unsigned stream = 0; stream < state.data->streams && stream < 4; stream++)
    ir.CreateStore(
        ir.CreateLoad(types._int, state.streams[stream].primitives),
        ir.CreateGEP(types._int, counts, {ir.CreateAdd(state.index, ir.getInt32(stream))})
    );
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
