#pragma once
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Error.h"
#include "air_builder.hpp"
#include "../airconv_public.h"
#include "../air_signature.hpp"
#include "../air_type.hpp"
#include "../shader_common.hpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace dxmt::dxbc {

struct io_binding_map;

/* What a pass of a geometry shader that streams out keeps while the shader
   runs (SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA) */
struct GeometryStreamOutput {
  const SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA *data;
  uint32_t vertices_per_primitive;
  /* the dxmt_geometry_stream_output */
  llvm::Value *record;
  /* where the threadgroup's count is, in the order of the draw */
  llvm::Value *index;
  /* write pass: what the count pass counted, and the position of the
     threadgroup's first primitive */
  llvm::Value *count;
  llvm::Value *first;
  /* vertices emitted since the last cut, and primitives completed */
  llvm::Value *strip_vertices;
  llvm::Value *primitives;
  /* write pass: the streamed out components of the two vertices before */
  llvm::Value *previous;
};

class IOHelper {
public:
  IOHelper(llvm::air::AIRBuilder &air, llvm::Function *function, air::AirType &types, io_binding_map &io_legacy);

  llvm::Error InitializeInput(
      uint32_t FromFunctionArgumentIndex, uint32_t ToRegister, uint32_t Mask, bool InvertWComponent = false
  );

  llvm::Error InitializeInterpolatedInput(
      uint32_t FromFunctionArgumentIndex, uint32_t ToRegister, uint32_t Mask,
      air::Interpolation Interpolation, uint32_t AtSampleIndex
  );

  llvm::Error PullVertexInput(
      uint32_t VertexBufferTableArgumentIndex, uint32_t ToRegister, uint32_t Mask,
      SM50_IA_INPUT_ELEMENT ElementInfo, uint32_t SlotMask
  );

  llvm::Error InitializeVertexIDInput(uint32_t ToRegister, uint32_t Mask);

  llvm::Error InitializeInstanceIDInput(uint32_t ToRegister, uint32_t Mask);

  llvm::Error InitializeInputAttribute(shader::common::InputAttribute Value, uint32_t FromFunctionArgumentIndex);

  llvm::Error CreateDepthOutputRegister();

  /* Stores the depth of the position input with the rasterizer's depth bias
     added into a new depth output register; the bias argument is the float4 of
     SM50_BINDING_INDEX_DEPTH_BIAS. With DepthArgument greater or less, the
     stored depth is kept on that side of the position's depth */
  llvm::Error CreateBiasedDepthOutputRegister(
      uint32_t PositionArgumentIndex, uint32_t BiasArgumentIndex, air::DepthArgument DepthArgument
  );

  llvm::Error CreateStencilRefOutputRegister();

  llvm::Error CreateCoverageMaskOutputRegister();

  llvm::Value *
  PopulateOutput(uint32_t FromRegister, uint32_t Mask, llvm::Value *StructRet, uint32_t AtElement);

  llvm::Value *
  PopulateOutputFixUnorm(uint32_t FromRegister, uint32_t Mask, llvm::Value *StructRet, uint32_t AtElement);

  llvm::Value *
  PopulateOutputSanitizePosition(uint32_t FromRegister, uint32_t Mask, llvm::Value *StructRet, uint32_t AtElement);

  /* A position outside the clip volume */
  llvm::Value *
  PopulateOutputClippedPosition(llvm::Value *StructRet, uint32_t AtElement);

  llvm::Value *
  PopulateOutputDepth(llvm::Value *StructRet, uint32_t AtElement);

  llvm::Value *
  PopulateOutputStencilRef(llvm::Value *StructRet, uint32_t AtElement);

  llvm::Value *
  PopulateOutputCoverageMask(llvm::Value *StructRet, uint32_t AtElement, uint32_t SampleMask);

  llvm::Value *
  PopulateDefaultCoverageMask(llvm::Value *StructRet, uint32_t AtElement, uint32_t SampleMask);

  /* Takes the vertex and instance ids of the vertex shader from the output
     vertex the stream output draw runs for: the draw is expanded to a list,
     and an indexed one loads its index here. Returns the draw's
     dxmt_stream_output_draw and the position of the output vertex among all
     the draw writes, a 64-bit value */
  std::pair<llvm::Value *, llvm::Value *> InitializeStreamOutputVertex(
      const SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *VertexSO, uint32_t StreamOutputDrawArgumentIndex
  );

  /* OutputMasks has the components the vertex shader writes of each of its
     output registers */
  llvm::Value *PopulateOutputStreamOutput(
      llvm::Value *StructRet, const SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *VertexSO,
      const std::vector<uint8_t> &OutputMasks, llvm::Value *StreamOutputDraw, llvm::Value *OutputVertex
  );

  /* Index is the threadgroup's place in the draw; VerticesPerPrimitive
     follows from the output topology of the geometry shader */
  GeometryStreamOutput InitializeGeometryStreamOutput(
      const SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA *Data, uint32_t RecordArgumentIndex, llvm::Value *Index,
      uint32_t VerticesPerPrimitive
  );

  /* An emit of the stream: completes a primitive once the strip has enough
     vertices, which the write pass stores below the limit as a list */
  void EmitGeometryStreamOutput(GeometryStreamOutput &State);

  void CutGeometryStreamOutput(GeometryStreamOutput &State);

  /* The end of the shader: the count pass stores its count */
  void FinishGeometryStreamOutput(GeometryStreamOutput &State);

  void PopulateMeshOutputRenderTargetArrayIndex(uint32_t FromRegister, uint32_t Mask, llvm::Value *PrimitiveId);

  void PopulateMeshOutputViewportArrayIndex(uint32_t FromRegister, uint32_t Mask, llvm::Value *PrimitiveId);

  void PopulateMeshOutputPrimitiveID(uint32_t FromRegister, uint32_t Mask, llvm::Value *PrimitiveId);

  void PopulateMeshOutputPosition(uint32_t FromRegister, uint32_t Mask, llvm::Value *VertexId);

  void PopulateMeshOutputVertexData(
      uint32_t FromRegister, uint32_t Mask, uint32_t Index, llvm::Value *VertexId,
      air::MSLScalerOrVectorType DesiredType
  );

private:
  llvm::Value *LoadFromArrayAt(llvm::Value *Array, llvm::Value *Index);
  llvm::Value *LoadFromVec4ArrayMasked(llvm::Value *Array, llvm::Value *Index, uint32_t Mask);
  llvm::Error StoreAtVec4ArrayMasked(llvm::Value *Array, llvm::Value *Index, llvm::Value *MaybeVec4, uint32_t Mask);
  llvm::Value *ExtendToVec4(llvm::Value *Value);
  llvm::Value *ToDesiredTypeFromIntVec4(llvm::Value *Vec4, llvm::Type *Desired, uint32_t Mask);
  llvm::Value *GetFloat4Splat(float Value);

  llvm::air::AIRBuilder &air;
  llvm::IRBuilderBase &ir;
  llvm::Function *function;
  air::AirType &types;

  /* Legacy */
  io_binding_map &io;
};

using PrologueFn = std::function<llvm::Error(IOHelper &)>;

class Prologues {
public:
  void
  Add(PrologueFn &&Fn) {
    functions.push_back(std::move(Fn));
  }

  llvm::Error Run(IOHelper &IO);

private:
  std::vector<PrologueFn> functions;
};

using EpilogueFn = std::function<llvm::Expected<llvm::Value *>(llvm::Value *, IOHelper &)>;

class Epilogues {
public:
  void
  Add(EpilogueFn &&Fn) {
    functions.push_back(std::move(Fn));
  }

  llvm::Expected<llvm::Value *> Run(llvm::Type *TyRet, IOHelper &IO);

private:
  std::vector<EpilogueFn> functions;
};

} // namespace dxmt::dxbc
