#include "d3d11_query.hpp"
#include "dxmt_occlusion_query.hpp"

namespace dxmt {

class OcclusionQuery : public MTLD3DQueryBase<MTLD3D11OcclusionQuery> {
  using MTLD3DQueryBase<MTLD3D11OcclusionQuery>::MTLD3DQueryBase;

  QueryState state_ = QueryState::Signaled;

  Rc<VisibilityResultQuery> query_ = new VisibilityResultQuery();
  uint64_t accumulated_value_ = 0;

  virtual UINT STDMETHODCALLTYPE
  GetDataSize() override {
    return desc_.Query == D3D11_QUERY_OCCLUSION_PREDICATE ? sizeof(BOOL) : sizeof(UINT64);
  };

  virtual HRESULT
  GetData(void *data) override {
    if (state_ == QueryState::Building) {
      return DXGI_ERROR_INVALID_CALL;
    }
    if (state_ == QueryState::Signaled || query_->getValue(&accumulated_value_)) {
      if (desc_.Query == D3D11_QUERY_OCCLUSION_PREDICATE) {
        *((BOOL *)data) = accumulated_value_ != 0;
      } else {
        *((uint64_t *)data) = accumulated_value_;
      }
      query_ = new VisibilityResultQuery();
      state_ = QueryState::Signaled;
      return S_OK;
    }
    return S_FALSE;
  }

  virtual VisibilityResultQuery *
  Begin() override {
    if (state_ == QueryState::Signaled) {
      state_ = QueryState::Building;
      return query_.ptr();
    }
    if (state_ == QueryState::Issued) {
      // discard previous issued query
      state_ = QueryState::Building;
      query_ = new VisibilityResultQuery();
      return query_.ptr();
    }
    // FIXME: it's effectively ignoring  Begin() after Begin()
    return nullptr;
  };

  virtual VisibilityResultQuery *
  End() override {
    if (state_ == QueryState::Signaled) {
      // ignore  a single End()
      accumulated_value_ = 0;
      return nullptr;
    }
    if (state_ == QueryState::Issued) {
      // FIXME: it's effectively ignoring End() after End()
      return nullptr;
    }
    state_ = QueryState::Issued;
    return query_.ptr();
  };

  virtual void DoDeferredQuery(VisibilityResultQuery *deferred_query) override{
    accumulated_value_ = 0;
    state_ = QueryState::Issued;
    query_ = deferred_query;
  };
};

HRESULT
CreateOcclusionQuery(MTLD3D11Device *pDevice, const D3D11_QUERY_DESC1 *pDesc, ID3D11Query1 **ppQuery) {
  if (ppQuery) {
    *ppQuery = ref(new OcclusionQuery(pDevice, pDesc));
    return S_OK;
  }
  return S_FALSE;
}

class MTLD3D11TimestampQueryImpl : public MTLD3DQueryBase<MTLD3D11TimestampQuery> {
  using MTLD3DQueryBase<MTLD3D11TimestampQuery>::MTLD3DQueryBase;

  QueryState state_ = QueryState::Signaled;

  Rc<TimestampQuery> query_ = new TimestampQuery();
  uint64_t latest_value_ = 0;

  virtual UINT STDMETHODCALLTYPE
  GetDataSize() override {
    return sizeof(UINT64);
  };

  virtual HRESULT
  GetData(void *data) override {
    if (state_ == QueryState::Signaled || query_->getValue(&latest_value_)) {
      *((uint64_t *)data) = latest_value_;
      query_ = new TimestampQuery();
      state_ = QueryState::Signaled;
      return S_OK;
    }
    return S_FALSE;
  }

  virtual TimestampQuery *
  End() override {
    if (state_ == QueryState::Issued) {
      // discard previous query
      query_ = new TimestampQuery();
    }
    state_ = QueryState::Issued;
    return query_.ptr();
  };
};

HRESULT
CreateTimestampQuery(MTLD3D11Device *pDevice, const D3D11_QUERY_DESC1 *pDesc, ID3D11Query1 **ppQuery) {
  if (ppQuery) {
    *ppQuery = ref(new MTLD3D11TimestampQueryImpl(pDevice, pDesc));
    return S_OK;
  }
  return S_FALSE;
}

class StreamOutputQuery : public MTLD3DQueryBase<MTLD3D11StreamOutputQuery> {
  using MTLD3DQueryBase<MTLD3D11StreamOutputQuery>::MTLD3DQueryBase;

  // No data before the first End(), and none while building
  QueryState state_ = QueryState::Undefined;

  virtual UINT STDMETHODCALLTYPE
  GetDataSize() override {
    return IsPredicateQuery(desc_.Query) ? sizeof(BOOL) : sizeof(D3D11_QUERY_DATA_SO_STATISTICS);
  };

  virtual void
  Begin() override {
    state_ = QueryState::Building;
  };

  virtual void
  End() override {
    state_ = QueryState::Signaled;
  };

  virtual HRESULT
  GetData(void *data) override {
    if (state_ != QueryState::Signaled)
      return DXGI_ERROR_INVALID_CALL;
    if (data)
      memset(data, 0, GetDataSize());
    return S_OK;
  }
};

HRESULT
CreateStreamOutputQuery(MTLD3D11Device *pDevice, const D3D11_QUERY_DESC1 *pDesc, ID3D11Query1 **ppQuery) {
  if (ppQuery) {
    *ppQuery = ref(new StreamOutputQuery(pDevice, pDesc));
    return S_OK;
  }
  return S_FALSE;
}

} // namespace dxmt