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

class MTLD3D11StreamOutputQueryImpl : public MTLD3DQueryBase<MTLD3D11StreamOutputQuery> {
  using MTLD3DQueryBase<MTLD3D11StreamOutputQuery>::MTLD3DQueryBase;

  // No data before the first End(), and none while building
  QueryState state_ = QueryState::Undefined;

  Rc<StreamOutputQuery> query_;
  StreamOutputCounterSet value_{};

public:
  ~MTLD3D11StreamOutputQueryImpl() {
    // The encoding context still counts it as open
    if (query_.ptr() && state_ == QueryState::Building)
      query_->abandon();
  }

private:
  virtual UINT STDMETHODCALLTYPE
  GetDataSize() override {
    return IsPredicateQuery(desc_.Query) ? sizeof(BOOL) : sizeof(D3D11_QUERY_DATA_SO_STATISTICS);
  };

  virtual Rc<StreamOutputQuery>
  Begin(Rc<StreamOutputQuery> &abandoned) override {
    if (state_ == QueryState::Building)
      abandoned = std::move(query_);
    state_ = QueryState::Building;
    query_ = new StreamOutputQuery();
    return query_;
  };

  virtual Rc<StreamOutputQuery>
  End() override {
    if (state_ != QueryState::Building) {
      // As a Begin() with the End() right after it
      value_ = {};
      state_ = QueryState::Signaled;
      return {};
    }
    state_ = QueryState::Issued;
    return query_;
  };

  virtual HRESULT
  GetData(void *data) override {
    if (state_ == QueryState::Undefined || state_ == QueryState::Building)
      return DXGI_ERROR_INVALID_CALL;
    // Asked each time: a command list that executes again issues the query
    // again
    if (state_ == QueryState::Issued && !query_->getValue(value_))
      return S_FALSE;
    if (!data)
      return S_OK;
    // SO_STATISTICS is the query of stream 0, SO_OVERFLOW_PREDICATE the one
    // of all streams
    unsigned stream = 0;
    switch (desc_.Query) {
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
      stream = 1;
      break;
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
      stream = 2;
      break;
    case D3D11_QUERY_SO_STATISTICS_STREAM3:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
      stream = 3;
      break;
    default:
      break;
    }
    if (!IsPredicateQuery(desc_.Query)) {
      auto statistics = (D3D11_QUERY_DATA_SO_STATISTICS *)data;
      statistics->NumPrimitivesWritten = value_[stream].written;
      statistics->PrimitivesStorageNeeded = value_[stream].needed;
      return S_OK;
    }
    bool overflow = value_[stream].needed > value_[stream].written;
    if (desc_.Query == D3D11_QUERY_SO_OVERFLOW_PREDICATE) {
      for (auto &counters : value_)
        overflow = overflow || counters.needed > counters.written;
    }
    *(BOOL *)data = overflow;
    return S_OK;
  }
};

HRESULT
CreateStreamOutputQuery(MTLD3D11Device *pDevice, const D3D11_QUERY_DESC1 *pDesc, ID3D11Query1 **ppQuery) {
  if (ppQuery) {
    *ppQuery = ref(new MTLD3D11StreamOutputQueryImpl(pDevice, pDesc));
    return S_OK;
  }
  return S_FALSE;
}

} // namespace dxmt