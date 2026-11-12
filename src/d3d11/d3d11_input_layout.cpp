#include "d3d11_input_layout.hpp"
#include "d3d11_device_child.hpp"
#include "DXBCParser/DXBCUtils.h"
#include "d3d11_shader.hpp"
#include "d3d11_state_object.hpp"
#include "dxmt_format.hpp"
#include "sha1/sha1_util.hpp"
#include "util_math.hpp"
#include "log/log.hpp"
#include "../d3d10/d3d10_shader.hpp"

namespace dxmt {

HRESULT ExtractMTLInputLayoutElements(
    MTLD3D11Device *device, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength,
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, uint32_t NumElements,
    MTL_SHADER_INPUT_LAYOUT_ELEMENT_DESC *pInputLayout,
    uint32_t *pNumElementsOut) {

  using namespace microsoft;
  uint16_t append_offset[32] = {0};
  uint32_t register_mask = 0;

  CSignatureParser parser;
  HRESULT hr =
      DXBCGetInputSignature(pShaderBytecodeWithInputSignature, BytecodeLength, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParameters;
  auto num_parameters = parser.GetParameters(&pParameters);

  UINT attribute_count = 0;
  for (UINT j = 0; j < NumElements; j++) {
    auto &desc = pInputElementDescs[j];

    MTL_DXGI_FORMAT_DESC metal_format;
    if (FAILED(MTLQueryDXGIFormat(device->GetMTLDevice(), desc.Format, metal_format))) {
      ERR("CreateInputLayout: Unsupported vertex format: ", desc.Format);
      return E_FAIL;
    }

    if (!metal_format.AttributeFormat) {
      ERR("CreateInputLayout: Unsupported vertex format: ", desc.Format);
      return E_INVALIDARG;
    }
    if (!metal_format.BytesPerTexel) {
      ERR("CreateInputLayout: not an ordinary or packed format: ", desc.Format);
      return E_INVALIDARG;
    }
    auto aligned_byte_offset = desc.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT
                                   ? align(append_offset[desc.InputSlot], std::min(4u, metal_format.BytesPerTexel))
                                   : desc.AlignedByteOffset;
    append_offset[desc.InputSlot] = aligned_byte_offset + metal_format.BytesPerTexel;

    auto pSig = std::find_if(
        pParameters, pParameters + num_parameters,
        [&](const D3D11_SIGNATURE_PARAMETER &inputSig) {
          return desc.SemanticIndex == inputSig.SemanticIndex &&
                 strcasecmp(desc.SemanticName, inputSig.SemanticName) == 0;
        });
    if (pSig == pParameters + num_parameters)
      continue; // shader has no such input register, so skip it
    auto &inputSig = *pSig;
    auto &attribute = pInputLayout[attribute_count++];

    attribute.Format = metal_format.AttributeFormat;

    attribute.Slot = desc.InputSlot;
    attribute.Index = inputSig.Register;
    attribute.Offset = aligned_byte_offset;
    // the layout stride is provided in IASetVertexBuffer
    attribute.StepFunction = desc.InputSlotClass;
    attribute.InstanceStepRate =
        desc.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA
            ? desc.InstanceDataStepRate
            : 1;
    register_mask |= (1 << inputSig.Register);
  }
  for (UINT i = 0; i < num_parameters; i++) {
    auto &inputSig = pParameters[i];
    if (inputSig.SystemValue != D3D10_SB_NAME_UNDEFINED)
      continue; // ignore SIV & SGV
    // The input assembler generates these, also for a signature that does not
    // mark them as system values, such as a pixel shader's
    if (strcasecmp(inputSig.SemanticName, "SV_VertexID") == 0 ||
        strcasecmp(inputSig.SemanticName, "SV_InstanceID") == 0)
      continue;
    if (!(register_mask & (1 << inputSig.Register))) {
      WARN("CreateInputLayout: Vertex shader expects ", inputSig.SemanticName,
           "_", inputSig.SemanticIndex, " but it's not in input layout element descriptors");
      return E_INVALIDARG;
    }
  }
  *pNumElementsOut = attribute_count;

  return S_OK;
}

class MTLD3D11StreamOutputLayout final
    : public ManagedDeviceChild<IMTLD3D11StreamOutputLayout> {
public:
  MTLD3D11StreamOutputLayout(MTLD3D11Device *device,
                             const MTL_STREAM_OUTPUT_DESC &desc)
      : ManagedDeviceChild<IMTLD3D11StreamOutputLayout>(device), desc_(desc),
        null_gs(this), d3d10_(this) {
    Sha1HashState h;
    h.update(desc.Strides);
    h.update(desc.RasterizedStream);
    h.update(desc.Elements.size());
    for (auto &el : desc.Elements) {
        h.update(el);
    }
    sha1_ = h.final();
  }

  ~MTLD3D11StreamOutputLayout() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11GeometryShader) ||
        riid == __uuidof(IMTLD3D11StreamOutputLayout)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLD3D11Shader)) {
      *ppvObject = ref(&null_gs);
      return S_OK;
    }

    // what the D3D10 device's CreateGeometryShaderWithStreamOutput returns
    if (riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10GeometryShader)) {
      *ppvObject = ref(&d3d10_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11StreamOutputLayout), riid)) {
      WARN("IMTLD3D11StreamOutputLayout: Unknown interface query ",
           str::format(riid));
    }

    return E_NOINTERFACE;
  };

  uint32_t STDMETHODCALLTYPE
  GetStreamOutputElements(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC **ppElements,
                          uint32_t Strides[4]) override {
    *ppElements = desc_.Elements.data();
    memcpy(Strides, desc_.Strides, sizeof(desc_.Strides));
    return desc_.Elements.size();
  }

  Sha1Digest &Digest() override {
    return sha1_;
  };

  virtual UINT RasterizedStream() override {
    return desc_.RasterizedStream;
  }

private:
  MTL_STREAM_OUTPUT_DESC desc_;
  Sha1Digest sha1_;

  class NullGeometryShader : public IMTLD3D11Shader {
    MTLD3D11StreamOutputLayout *container;

  public:
    NullGeometryShader(MTLD3D11StreamOutputLayout *container) : container(container) {};

    ULONG STDMETHODCALLTYPE AddRef() override { return container->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return container->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
      return container->QueryInterface(riid, ppvObject);
    }

    virtual ManagedShader GetManagedShader() override { return nullptr; };
    void AddRefPrivate() override { container->AddRefPrivate(); }
    void ReleasePrivate() override { container->ReleasePrivate(); }
  };

  NullGeometryShader null_gs;
  MTLD3D10GeometryShader d3d10_;
};

/* What Windows checks of a stream output declaration besides the bytecode,
   as Wine's d3d11 and d3d10core tests record it */
HRESULT
ValidateStreamOutputDeclaration(
    const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pEntries, UINT NumEntries,
    const UINT *pStrides, UINT NumStrides, UINT RasterizedStream, D3D_FEATURE_LEVEL FeatureLevel
) {
  using namespace microsoft;
  if (NumEntries > D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT)
    return E_INVALIDARG;
  if (!pEntries != !NumEntries)
    return E_INVALIDARG;
  if (NumStrides && !pStrides)
    return E_INVALIDARG;
  if (RasterizedStream != D3D11_SO_NO_RASTERIZED_STREAM && RasterizedStream >= D3D11_SO_STREAM_COUNT)
    return E_INVALIDARG;
  if (FeatureLevel < D3D_FEATURE_LEVEL_11_0 && (RasterizedStream || NumStrides > 1 || (NumStrides && !NumEntries)))
    return E_INVALIDARG;
  if (!NumEntries)
    return S_OK;

  CSignatureParser5 parser;
  if (FAILED(DXBCGetOutputSignature(pShaderBytecode, BytecodeLength, &parser)))
    return E_INVALIDARG;

  for (unsigned i = 0; i < NumEntries; i++) {
    auto &entry = pEntries[i];
    if (entry.Stream >= D3D11_SO_STREAM_COUNT || (entry.Stream && FeatureLevel < D3D_FEATURE_LEVEL_11_0))
      return E_INVALIDARG;
    if (entry.OutputSlot >= D3D11_SO_BUFFER_SLOT_COUNT)
      return E_INVALIDARG;
    if (!entry.SemanticName) {
      if (entry.SemanticIndex || entry.StartComponent || !entry.ComponentCount)
        return E_INVALIDARG;
      continue;
    }
    if (!entry.ComponentCount || entry.StartComponent + entry.ComponentCount > 4)
      return E_INVALIDARG;
    const D3D11_SIGNATURE_PARAMETER *parameters;
    auto num_parameters = parser.Signature(entry.Stream)->GetParameters(&parameters);
    auto output = std::find_if(parameters, parameters + num_parameters, [&](const D3D11_SIGNATURE_PARAMETER &param) {
      return param.SemanticIndex == entry.SemanticIndex && strcasecmp(param.SemanticName, entry.SemanticName) == 0;
    });
    if (output == parameters + num_parameters)
      return E_INVALIDARG;
    // StartComponent counts from the first component the output has
    uint32_t output_mask = output->Mask & 0xf;
    uint32_t first_component = output_mask ? __builtin_ctz(output_mask) : 4;
    uint32_t mask = ((1u << entry.ComponentCount) - 1) << (first_component + entry.StartComponent);
    if ((output_mask & mask) != mask)
      return E_INVALIDARG;
    for (unsigned j = 0; j < i; j++) {
      auto &other = pEntries[j];
      if (other.SemanticName && other.Stream == entry.Stream && other.SemanticIndex == entry.SemanticIndex &&
          strcasecmp(other.SemanticName, entry.SemanticName) == 0 &&
          other.StartComponent < entry.StartComponent + entry.ComponentCount &&
          entry.StartComponent < other.StartComponent + other.ComponentCount)
        return E_INVALIDARG;
    }
  }

  for (unsigned stream = 0; stream < D3D11_SO_STREAM_COUNT; stream++) {
    uint32_t packed_size[D3D11_SO_BUFFER_SLOT_COUNT] = {};
    uint32_t element_count[D3D11_SO_BUFFER_SLOT_COUNT] = {};
    uint32_t gap_count[D3D11_SO_BUFFER_SLOT_COUNT] = {};
    for (unsigned i = 0; i < NumEntries; i++) {
      auto &entry = pEntries[i];
      if (entry.Stream != stream)
        continue;
      packed_size[entry.OutputSlot] += sizeof(float) * entry.ComponentCount;
      element_count[entry.OutputSlot]++;
      if (!entry.SemanticName)
        gap_count[entry.OutputSlot]++;
    }
    for (unsigned slot = 0; slot < D3D11_SO_BUFFER_SLOT_COUNT; slot++) {
      if (!element_count[slot])
        continue;
      if (element_count[slot] == gap_count[slot])
        return E_INVALIDARG;
      if (NumStrides && (NumStrides <= slot || pStrides[slot] < packed_size[slot] || pStrides[slot] % 4))
        return E_INVALIDARG;
    }
    // Below feature level 11_0, a declaration that writes several slots
    // writes one element to each
    if (!stream && FeatureLevel < D3D_FEATURE_LEVEL_11_0 && element_count[0] != NumEntries) {
      for (unsigned slot = 0; slot < D3D11_SO_BUFFER_SLOT_COUNT; slot++) {
        if (element_count[slot] > 1)
          return E_INVALIDARG;
      }
    }
  }

  return S_OK;
}

HRESULT ExtractMTLStreamOutputElements(
    MTLD3D11Device *pDevice, const void *pShaderBytecode, SIZE_T BytecodeLength, UINT NumEntries,
    const D3D11_SO_DECLARATION_ENTRY *pEntries,
    MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC *pStreamOut,
    uint32_t *pNumElementsOut) {
  using namespace microsoft;
  std::array<uint32_t, 4> offsets = {{}};
  uint32_t element_count = 0;
  CSignatureParser parser;
  HRESULT hr = DXBCGetOutputSignature(pShaderBytecode, BytecodeLength, &parser);
  if (FAILED(hr)) {
    return hr;
  }
  const D3D11_SIGNATURE_PARAMETER *pParameters;
  auto numParameters = parser.GetParameters(&pParameters);
  for (unsigned i = 0; i < NumEntries; i++) {
    auto entry = pEntries[i];
    if (entry.Stream != 0) {
      WARN("CreateEmulatedVertexStreamOutputShader: only stream 0 supported");
      return E_INVALIDARG;
    }
    // FIXME: support more than 1 output slot
    if (entry.OutputSlot != 0) {
      WARN("CreateEmulatedVertexStreamOutputShader: only slot 0 supported");
      return E_INVALIDARG;
    }
    if (entry.SemanticName == nullptr) {
      offsets[entry.OutputSlot] += sizeof(float) * entry.ComponentCount;
      continue;
    }
    auto pDesc = std::find_if(
        pParameters, pParameters + numParameters,
        [&](const D3D11_SIGNATURE_PARAMETER &Ele) {
          return Ele.SemanticIndex == entry.SemanticIndex &&
                 strcasecmp(Ele.SemanticName, entry.SemanticName) == 0;
        });
    if (pDesc == pParameters + numParameters) {
      ERR("CreateEmulatedVertexStreamOutputShader: output parameter not found");
      return E_INVALIDARG;
    }
    auto reg_id = pDesc->Register;
    // StartComponent counts from the first component of the output, which
    // shares its register with others when the signature packs them
    uint32_t first_component = (pDesc->Mask & 0xf) ? __builtin_ctz(pDesc->Mask & 0xf) : 0;
    for (unsigned i = 0; i < entry.ComponentCount; i++) {
      auto offset = offsets[entry.OutputSlot];
      offsets[entry.OutputSlot] += sizeof(float);
      auto component = first_component + entry.StartComponent + i;
      pStreamOut[element_count++] = {.Register = reg_id,
                                     .Component = component,
                                     .OutputSlot = entry.OutputSlot,
                                     .Offset = offset};
    }
  }
  *pNumElementsOut = element_count;
  return S_OK;
}

template <>
HRESULT StateObjectCache<MTL_STREAM_OUTPUT_DESC, IMTLD3D11StreamOutputLayout>::
    CreateStateObject(const MTL_STREAM_OUTPUT_DESC *pSOLayoutDesc,
                      IMTLD3D11StreamOutputLayout **ppSOLayout) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);
  InitReturnPtr(ppSOLayout);

  if (!pSOLayoutDesc)
    return E_INVALIDARG;

  if (!ppSOLayout)
    return S_FALSE;

  if (cache.contains(*pSOLayoutDesc)) {
    cache.at(*pSOLayoutDesc)->QueryInterface(IID_PPV_ARGS(ppSOLayout));
    return S_OK;
  }

  cache.emplace(*pSOLayoutDesc, std::make_unique<MTLD3D11StreamOutputLayout>(
                                    device, *pSOLayoutDesc));
  cache.at(*pSOLayoutDesc)->QueryInterface(IID_PPV_ARGS(ppSOLayout));

  return S_OK;
}

} // namespace dxmt