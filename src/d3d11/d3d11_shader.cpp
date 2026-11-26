#include "d3d11_shader.hpp"
#include "Metal.hpp"
#include "airconv_public.h"
#include "config/config.hpp"
#include "d3d11_input_layout.hpp"
#include "sha1/sha1_util.hpp"
#include <mutex>

namespace dxmt {

/* Salt of the geometry pipeline variants that stream out, whose code only
   our series generates. Raise it whenever the series changes the code
   generated for them: the shader cache is keyed on AIRCONV_VERSION, which
   follows upstream, and would go on handing out the code compiled before. */
static constexpr uint32_t kGeometryStreamOutput = 1;

/* Salt of the other geometry pipeline variants whose code our series
   changes: those without stream output of a geometry shader that declares a
   stream other than 0, and those of a vertex shader with UAVs for a strip
   topology. It enters only their keys, so that the cached variants of every
   other shader stay valid; raise it as kGeometryStreamOutput. */
static constexpr uint32_t kGeometryOwnVariants = 1;

SM50_SHADER_FLAG
getGlobalShaderFlag() {
  static SM50_SHADER_FLAG shader_flag;
  static std::once_flag get_shader_flag_once;
  std::call_once(get_shader_flag_once, []() {
    shader_flag = {};
    if (Config::getInstance().getOption<bool>("d3d11.sampleNaNToZero", false))
      shader_flag |= SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO;
    if (Config::getInstance().getOption<bool>("d3d11.defuseFma", false))
      shader_flag |= SM50_SHADER_FLAG_DEFUSE_FMA;
  });
  return shader_flag;
};

template <typename Proc>
class GeneralShaderCompileTask : public CompiledShader {
public:
  GeneralShaderCompileTask(MTLD3D11Device *pDevice, ManagedShader shader,
                           Proc &&proc, std::string func_name, const Sha1Digest& variant_digest)
      : CompiledShader(), proc(std::forward<Proc>(proc)), func_name(func_name), device_(pDevice),
        shader_(shader), variant_digest_(variant_digest) {
    sm50_common.type = SM50_SHADER_COMMON;
    sm50_common.metal_version = (SM50_SHADER_METAL_VERSION)pDevice->GetDXMTDevice().metalVersion();
    sm50_common.flags = getGlobalShaderFlag();
    sm50_common.next = nullptr;
  }

  ~GeneralShaderCompileTask() {}

  bool GetShader(MTL_COMPILED_SHADER *pShaderData) final {
    bool ret = false;
    if ((ret = ready_.load(std::memory_order_acquire))) {
      *pShaderData = {function_};
    }
    return ret;
  }

  ThreadpoolWork *
  RunThreadpoolWork() {
    auto pool = WMT::MakeAutoreleasePool();
    WMT::Reference<WMT::Error> err;
    WMT::Reference<WMT::DispatchData> lib_data = shader_->find_cached_variant(variant_digest_);

    while (lib_data != nullptr) {
      auto library = device_->GetMTLDevice().newLibrary(lib_data, err);

      if (err || !library) {
        ERR("Failed to create MTLLibrary from cache: ", err.description().getUTF8String());
        lib_data = nullptr;
        break;
      }

      function_ = library.newFunction(func_name.c_str());

      if (function_ == nullptr) {
        ERR("Failed to create MTLFunction from cache: ", func_name);
        lib_data = nullptr;
        break;
      }
      break;
    }

    if (!lib_data) {
      SM50_COMPILED_BITCODE bitcode;
      sm50_bitcode_t compile_result = proc(func_name.c_str(), &sm50_common);

      if (!compile_result)
        return this;

      SM50GetCompiledBitcode(compile_result, &bitcode);
      lib_data = WMT::MakeDispatchData(bitcode.Data, bitcode.Size);
      auto library = device_->GetMTLDevice().newLibrary(lib_data, err);

      if (err) {
        ERR("Failed to create MTLLibrary: ", err.description().getUTF8String());
        shader_->dump();
        return this;
      }

      shader_->update_cached_variant(variant_digest_, lib_data);

      SM50DestroyBitcode(compile_result);
      function_ = library.newFunction(func_name.c_str());

      if (function_ == nullptr) {
        ERR("Failed to create MTLFunction: ", func_name);
        shader_->dump();
      }
    }

    return this;
  }

  bool GetIsDone() { return ready_; }

  void SetIsDone(bool state) { ready_.store(state); }

private:
  SM50_SHADER_COMMON_DATA sm50_common;
  Proc proc;
  std::string func_name;
  MTLD3D11Device *device_;
  ManagedShader shader_;
  Sha1Digest variant_digest_;
  std::atomic_bool ready_;
  WMT::Reference<WMT::Function> function_;
};

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantVertex variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.gs_passthrough);
  h.update(variant.rasterization_disabled);
  if (variant.input_layout_handle)
    h.update(variant.input_layout_handle->sha1());
  auto variant_digest = h.final();
  std::string func_name = "vs_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();

  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_IA_INPUT_LAYOUT_DATA data_ia_layout;
    SM50_SHADER_GS_PASS_THROUGH_DATA data_gs_passthrough;
    data_gs_passthrough.type = SM50_SHADER_GS_PASS_THROUGH;
    data_gs_passthrough.DataEncoded = variant.gs_passthrough;
    data_gs_passthrough.RasterizationDisabled = variant.rasterization_disabled;
    data_gs_passthrough.next = common;
    if (variant.input_layout_handle) {
      data_gs_passthrough.next = &data_ia_layout;
      data_ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
      data_ia_layout.next = common;
      data_ia_layout.slot_mask = variant.input_layout_handle->input_slot_mask();
      data_ia_layout
          .num_elements = variant.input_layout_handle->input_layout_element(
          (MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC **)&data_ia_layout.elements);
    }

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50Compile(
            shader->handle(),
            (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data_gs_passthrough,
            func_name, &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
};

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantPixel variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.sample_mask);
  h.update(variant.unorm_output_reg_mask);
  h.update(variant.dual_source_blending);
  h.update(variant.disable_depth_output);
  // Hashed only when set, so that the cached variants without the bias stay
  // valid
  if (variant.depth_bias)
    h.update(variant.depth_bias);
  auto variant_digest = h.final();
  std::string func_name = "ps_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();

  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_PSO_PIXEL_SHADER_DATA data;
    data.type = SM50_SHADER_PSO_PIXEL_SHADER;
    data.next = common;
    data.sample_mask = variant.sample_mask;
    data.dual_source_blending = variant.dual_source_blending;
    data.disable_depth_output = variant.disable_depth_output;
    data.depth_bias = variant.depth_bias;
    data.unorm_output_reg_mask = variant.unorm_output_reg_mask;
    memset(data.pixel_formats, 0, sizeof(data.pixel_formats));

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50Compile(shader->handle(),
                               (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data,
                               func_name, &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
};

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantDefault) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  auto variant_digest = h.final();
  std::string func_name = "cs_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50Compile(shader->handle(), (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)common, func_name,
                               &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
};

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantTessellationVertexHull variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.vertex_shader_handle->sha1());
  h.update(variant.index_buffer_format);
  h.update(variant.max_potential_tess_factor);
  if (variant.input_layout_handle)
    h.update(variant.input_layout_handle->sha1());
  auto variant_digest = h.final();
  std::string func_name = "vshs_" + shader->sha1().string().substr(0, 8) + "_" +  variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout;
    SM50_SHADER_PSO_TESSELLATOR_DATA pso_tess;
    ia_layout.index_buffer_format = variant.index_buffer_format;
    if (variant.input_layout_handle) {
      ia_layout.slot_mask = variant.input_layout_handle->input_slot_mask();
      ia_layout.num_elements = variant.input_layout_handle->input_layout_element(
          reinterpret_cast<MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC **>(&ia_layout.elements)
      );
    } else {
      ia_layout.slot_mask = 0;
      ia_layout.num_elements = 0;
    }
    ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
    ia_layout.next = &pso_tess;
    pso_tess.type = SM50_SHADER_PSO_TESSELLATOR;
    pso_tess.next = common;
    pso_tess.max_potential_tess_factor = variant.max_potential_tess_factor;

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50CompileTessellationPipelineHull(
            variant.vertex_shader_handle->handle(), shader->handle(), 
            (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&ia_layout, func_name,
            &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
}

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantTessellationDomain variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.hull_shader_handle->sha1());
  h.update(variant.gs_passthrough);
  h.update(variant.rasterization_disabled);
  h.update(variant.max_potential_tess_factor);
  auto variant_digest = h.final();
  std::string func_name = "ds_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_GS_PASS_THROUGH_DATA gs_passthrough;
    SM50_SHADER_PSO_TESSELLATOR_DATA pso_tess;
    gs_passthrough.type = SM50_SHADER_GS_PASS_THROUGH;
    gs_passthrough.DataEncoded = variant.gs_passthrough;
    gs_passthrough.RasterizationDisabled = variant.rasterization_disabled;
    gs_passthrough.next = &pso_tess;
    pso_tess.type = SM50_SHADER_PSO_TESSELLATOR;
    pso_tess.next = common;
    pso_tess.max_potential_tess_factor = variant.max_potential_tess_factor;

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50CompileTessellationPipelineDomain(
            variant.hull_shader_handle->handle(), shader->handle(),
            (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&gs_passthrough, func_name,
            &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
}

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantVertexStreamOutput variant) {
  // Salt of the variants that take their draw from a dxmt_stream_output_draw,
  // so that the shader cache serves none compiled for another layout of what
  // the function reads
  static constexpr char kStreamOutputDraw[] = "stream output draw of 4 slots, vertex limit, indices, strip cuts";
  Sha1HashState h;
  h.update(kStreamOutputDraw);
  h.update(getGlobalShaderFlag());
  h.update(variant.topology);
  h.update(variant.rasterized);
  h.update(((IMTLD3D11StreamOutputLayout *)variant.stream_output_layout_handle)->Digest());
  if (variant.input_layout_handle)
    h.update(variant.input_layout_handle->sha1());
  // Hashed only when set, so that the cached variants without it stay valid
  if (variant.gs_passthrough != ~0u)
    h.update(variant.gs_passthrough);
  auto variant_digest = h.final();
  std::string func_name = "vsso_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA data_so;
    SM50_SHADER_IA_INPUT_LAYOUT_DATA data_vertex_pulling;
    SM50_SHADER_GS_PASS_THROUGH_DATA data_gs_passthrough;
    data_so.type = SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT;
    data_so.next = common;
    // The vertex function returns the render target array and viewport
    // indices from the registers a pass-through geometry shader takes them
    // from
    if (variant.gs_passthrough != ~0u) {
      data_gs_passthrough.type = SM50_SHADER_GS_PASS_THROUGH;
      data_gs_passthrough.DataEncoded = variant.gs_passthrough;
      data_gs_passthrough.RasterizationDisabled = !variant.rasterized;
      data_gs_passthrough.next = common;
      data_so.next = &data_gs_passthrough;
    }
    data_so.num_output_slots = 0;
    data_so.topology = variant.topology;
    data_so.rasterized = variant.rasterized;
    data_so.num_elements =
        ((IMTLD3D11StreamOutputLayout *)variant.stream_output_layout_handle)
            ->GetStreamOutputElements(
                (MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC **)&data_so.elements,
                data_so.strides);

    if (variant.input_layout_handle) {
      data_vertex_pulling.type = SM50_SHADER_IA_INPUT_LAYOUT;
      data_vertex_pulling.next = data_so.next;
      data_so.next = &data_vertex_pulling;
      data_vertex_pulling.slot_mask =
          variant.input_layout_handle->input_slot_mask();
      data_vertex_pulling.num_elements =
          variant.input_layout_handle->input_layout_element(
              (MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC **)&data_vertex_pulling
                  .elements);
    }

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50Compile(
            shader->handle(), (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&data_so,
            func_name, &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
};

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantGeometryVertex variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.geometry_shader_handle->sha1());
  h.update(variant.index_buffer_format);
  h.update(variant.strip_topology);
  if (variant.input_layout_handle)
    h.update(variant.input_layout_handle->sha1());
  if (variant.stream_output) {
    h.update(kGeometryStreamOutput);
    h.update(variant.stream_output_pass);
  }
  // The threads that shade a strip's vertex a second time leave out its UAV
  // stores and atomics, except in the count pass, which leaves them out
  // everywhere
  if (variant.strip_topology && shader->reflection().UAVSlotMask &&
      !(variant.stream_output && variant.stream_output_pass == SM50_GEOMETRY_STREAM_OUTPUT_COUNT))
    h.update(kGeometryOwnVariants);
  auto variant_digest = h.final();
  std::string func_name = "vsgs_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA stream_output = {};
    stream_output.type = SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT;
    stream_output.next = common;
    stream_output.pass = variant.stream_output_pass;
    SM50_SHADER_IA_INPUT_LAYOUT_DATA ia_layout;
    ia_layout.index_buffer_format = variant.index_buffer_format;
    if (variant.input_layout_handle) {
      ia_layout.slot_mask = variant.input_layout_handle->input_slot_mask();
      ia_layout.num_elements =
          variant.input_layout_handle->input_layout_element(
              (MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC **)&ia_layout.elements);
    } else {
      ia_layout.slot_mask = 0;
      ia_layout.num_elements = 0;
    }

    ia_layout.type = SM50_SHADER_IA_INPUT_LAYOUT;
    ia_layout.next = variant.stream_output ? (void *)&stream_output : (void *)common;

    SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry;
    geometry.type = SM50_SHADER_PSO_GEOMETRY_SHADER;
    geometry.next = &ia_layout;
    geometry.strip_topology = variant.strip_topology;

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50CompileGeometryPipelineVertex(
            shader->handle(), variant.geometry_shader_handle->handle(),
            (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&geometry, func_name,
            &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
}

template <>
std::unique_ptr<CompiledShader>
CreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,
                    ShaderVariantGeometry variant) {
  Sha1HashState h;
  h.update(getGlobalShaderFlag());
  h.update(variant.vertex_shader_handle->sha1());
  h.update(variant.strip_topology);
  auto so_layout = (IMTLD3D11StreamOutputLayout *)variant.stream_output_layout_handle;
  if (so_layout) {
    h.update(kGeometryStreamOutput);
    h.update(so_layout->Digest());
    h.update(variant.stream_output_pass);
    h.update(variant.rasterized);
  } else if (shader->reflection().GeometryShader.Streams & ~1u) {
    // Without stream output only stream 0 is drawn
    h.update(kGeometryOwnVariants);
  }
  auto variant_digest = h.final();
  std::string func_name = "gs_" + shader->sha1().string().substr(0, 8) + "_" + variant_digest.string();
  auto proc = [=](const char *func_name, SM50_SHADER_COMMON_DATA *common) -> sm50_bitcode_t  {
    SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT_DATA stream_output = {};
    stream_output.type = SM50_SHADER_EMULATE_GEOMETRY_STREAM_OUTPUT;
    stream_output.next = common;
    stream_output.pass = variant.stream_output_pass;
    stream_output.rasterized = variant.rasterized;
    if (so_layout) {
      stream_output.num_elements = so_layout->GetStreamOutputElements(
          (MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC **)&stream_output.elements, stream_output.strides
      );
      stream_output.streams = so_layout->GetSlotStreams(stream_output.slot_streams);
      stream_output.rasterized_stream = so_layout->RasterizedStream();
    }
    SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry;
    geometry.type = SM50_SHADER_PSO_GEOMETRY_SHADER;
    geometry.next = so_layout ? (void *)&stream_output : (void *)common;
    geometry.strip_topology = variant.strip_topology;

    sm50_bitcode_t compile_result = nullptr;
    sm50_error_t sm50_err = nullptr;
    if (SM50CompileGeometryPipelineGeometry(
            variant.vertex_shader_handle->handle(), shader->handle(),
            (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&geometry, func_name,
            &compile_result, &sm50_err)) {
      ERR("Failed to compile shader: ", SM50GetErrorMessageString(sm50_err));
      SM50FreeError(sm50_err);
      return nullptr;
    }
    return compile_result;
  };
  return std::make_unique<GeneralShaderCompileTask<decltype(proc)>>(
      pDevice, shader, std::move(proc), func_name, variant_digest);
}

} // namespace dxmt