/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "Metal.hpp"
#include "airconv_public.h"
#include "d3d11_device.hpp"
#include "d3d11_pipeline.hpp"
#include "log/log.hpp"

namespace dxmt {

class MTLCompiledGeometryPipelineImpl
    : public MTLCompiledGeometryPipeline {
public:
  MTLCompiledGeometryPipelineImpl(MTLD3D11Device *pDevice,
                              const MTL_GRAPHICS_PIPELINE_DESC *pDesc)
      : num_rtvs(pDesc->NumColorAttachments),
        depth_stencil_format(pDesc->DepthStencilFormat), device_(pDevice),
        pBlendState(pDesc->BlendState),
        RasterizationEnabled(pDesc->RasterizationEnabled),
        SampleCount(pDesc->SampleCount),
        ShaderDepthBias(pDesc->ShaderDepthBias) {
    if (pDesc->RasterizationEnabled && !pDesc->PixelShader)
      num_rtvs = 0;
    uint32_t unorm_output_reg_mask = 0;
    for (unsigned i = 0; i < num_rtvs; i++) {
      rtv_formats[i] = pDesc->ColorAttachmentFormats[i];
      unorm_output_reg_mask |= (uint32_t(IsUnorm8RenderTargetFormat(
                                    pDesc->ColorAttachmentFormats[i]))
                                << i);
    }
    VertexShader =
        pDesc->VertexShader->get_shader(ShaderVariantGeometryVertex{
            pDesc->InputLayout, pDesc->GeometryShader,
            pDesc->IndexBufferFormat, pDesc->GSStripTopology, pDesc->SOLayout != nullptr,
            SM50_GEOMETRY_STREAM_OUTPUT_WRITE});
    GeometryShader = pDesc->GeometryShader->get_shader(ShaderVariantGeometry{
        pDesc->VertexShader, pDesc->GSStripTopology, (uint64_t)pDesc->SOLayout, SM50_GEOMETRY_STREAM_OUTPUT_WRITE,
        pDesc->SOLayout && pDesc->RasterizationEnabled});
    // The pass that counts what a draw that streams out writes
    CountVertexShader = nullptr;
    CountGeometryShader = nullptr;
    if (pDesc->SOLayout) {
      // It differs from the drawing one only without the UAV stores and
      // atomics, so a vertex shader without UAVs counts with that one
      if (pDesc->VertexShader->reflection().UAVSlotMask)
        CountVertexShader = pDesc->VertexShader->get_shader(ShaderVariantGeometryVertex{
            pDesc->InputLayout, pDesc->GeometryShader, pDesc->IndexBufferFormat, pDesc->GSStripTopology, true,
            SM50_GEOMETRY_STREAM_OUTPUT_COUNT});
      CountGeometryShader = pDesc->GeometryShader->get_shader(ShaderVariantGeometry{
          pDesc->VertexShader, pDesc->GSStripTopology, (uint64_t)pDesc->SOLayout, SM50_GEOMETRY_STREAM_OUTPUT_COUNT,
          false});
    }

    if (pDesc->PixelShader) {
      PixelShader = pDesc->PixelShader->get_shader(ShaderVariantPixel{
          pDesc->SampleMask, pDesc->BlendState->IsDualSourceBlending(),
          depth_stencil_format == WMTPixelFormatInvalid,
          unorm_output_reg_mask, pDesc->ShaderDepthBias});
      ps_valid_render_targets = pDesc->PixelShader->reflection().PSValidRenderTargets;
    } else {
      PixelShader = nullptr;
      ps_valid_render_targets = 0;
    }
  }

  void GetPipeline(MTL_COMPILED_GRAPHICS_PIPELINE *pPipeline) final {
    ready_.wait(false, std::memory_order_acquire);
    *pPipeline = {state_mesh_, state_count_};
  }

  ThreadpoolWork *RunThreadpoolWork() {

    WMT::Reference<WMT::Error> err;
    MTL_COMPILED_SHADER vs, gs, ps;

    if (!VertexShader->GetShader(&vs)) {
      return VertexShader;
    }
    if (!vs.Function) {
      ERR("Failed to create mesh PSO: Invalid vertex shader.");
      return this;
    }
    if (!GeometryShader->GetShader(&gs)) {
      return GeometryShader;
    }
    if (!gs.Function) {
      ERR("Failed to create mesh PSO: Invalid geometry shader.");
      return this;
    }
    MTL_COMPILED_SHADER count_vs = vs, count_gs;
    if (CountVertexShader) {
      if (!CountVertexShader->GetShader(&count_vs)) {
        return CountVertexShader;
      }
      if (!count_vs.Function) {
        ERR("Failed to create mesh PSO: Invalid vertex shader for stream output.");
        return this;
      }
    }
    if (CountGeometryShader) {
      if (!CountGeometryShader->GetShader(&count_gs)) {
        return CountGeometryShader;
      }
      if (!count_gs.Function) {
        ERR("Failed to create mesh PSO: Invalid geometry shader for stream output.");
        return this;
      }
    }
    if (PixelShader) {
      if (!PixelShader->GetShader(&ps)) {
        return PixelShader;
      }
      if (!ps.Function) {
        ERR("Failed to create mesh PSO: Invalid pixel shader.");
        return this;
      }
    }

    WMTMeshRenderPipelineInfo info;
    WMT::InitializeMeshRenderPipelineInfo(info);

    info.object_function = vs.Function;
    info.mesh_function = gs.Function;
    info.payload_memory_length = 16256;

    info.immutable_object_buffers = (1 << 16)  | (1 << 21) | (1 << 29) | (1 << 30);
    info.immutable_mesh_buffers = (1 << 29) | (1 << 30);
    info.immutable_fragment_buffers = (1 << 29) | (1 << 30);

    WMT::Reference<WMT::Function> depth_bias_function;
    if (PixelShader) {
      info.fragment_function = ps.Function;
    } else if (ShaderDepthBias) {
      depth_bias_function = GetDepthBiasFragmentFunction(device_, ShaderDepthBias);
      info.fragment_function = depth_bias_function;
    }
    info.rasterization_enabled = RasterizationEnabled;

    for (unsigned i = 0; i < num_rtvs; i++) {
      if (rtv_formats[i] == WMTPixelFormatInvalid)
        continue;
      info.colors[i].pixel_format = rtv_formats[i];
    }

    if (depth_stencil_format != WMTPixelFormatInvalid) {
      info.depth_pixel_format = depth_stencil_format;
    }
    if (DepthStencilPlanarFlags(depth_stencil_format) & 2) {
      info.stencil_pixel_format = depth_stencil_format;
    }

    if (pBlendState) {
      pBlendState->SetupMetalPipelineDescriptor((WMTRenderPipelineBlendInfo *)&info, num_rtvs, ps_valid_render_targets);
    }
    // Without a fragment function there was no coverage from alpha, and this
    // one writes no alpha
    if (depth_bias_function)
      info.alpha_to_coverage_enabled = false;

    info.raster_sample_count = SampleCount;

    state_mesh_ = device_->GetMTLDevice().newRenderPipelineState(info, err);

    if (state_mesh_ == nullptr) {
      ERR("Failed to create mesh PSO: ", err.description().getUTF8String());
      return this;
    }

    if (CountGeometryShader) {
      WMTMeshRenderPipelineInfo count_info;
      WMT::InitializeMeshRenderPipelineInfo(count_info);
      count_info.object_function = count_vs.Function;
      count_info.mesh_function = count_gs.Function;
      count_info.payload_memory_length = info.payload_memory_length;
      count_info.immutable_object_buffers = info.immutable_object_buffers;
      count_info.immutable_mesh_buffers = info.immutable_mesh_buffers;
      count_info.rasterization_enabled = false;
      state_count_ = device_->GetMTLDevice().newRenderPipelineState(count_info, err);
      if (state_count_ == nullptr) {
        ERR("Failed to create mesh PSO for stream output: ", err.description().getUTF8String());
        state_mesh_ = nullptr;
        return this;
      }
    }
    return this;
  }

  bool GetIsDone() { return ready_; }

  void SetIsDone(bool state) {
    ready_.store(state);
    ready_.notify_all();
  }

private:
  UINT num_rtvs;
  UINT ps_valid_render_targets;
  WMTPixelFormat rtv_formats[8];
  WMTPixelFormat depth_stencil_format;
  MTLD3D11Device *device_;
  std::atomic_bool ready_;
  IMTLD3D11BlendState *pBlendState;
  WMT::Reference<WMT::RenderPipelineState> state_mesh_;
  WMT::Reference<WMT::RenderPipelineState> state_count_;
  bool RasterizationEnabled;
  UINT SampleCount;
  uint8_t ShaderDepthBias;

  CompiledShader *VertexShader;
  CompiledShader *PixelShader;
  CompiledShader *GeometryShader;
  CompiledShader *CountVertexShader;
  CompiledShader *CountGeometryShader;
};

std::unique_ptr<MTLCompiledGeometryPipeline>
CreateGeometryPipeline(MTLD3D11Device *pDevice,
                       MTL_GRAPHICS_PIPELINE_DESC *pDesc) {
  return std::make_unique<MTLCompiledGeometryPipelineImpl>(pDevice, pDesc);
}

} // namespace dxmt