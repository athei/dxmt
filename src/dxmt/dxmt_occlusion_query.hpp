#pragma once

#include "Metal.hpp"
#include "rc/util_rc_ptr.hpp"
#include "thread.hpp"
#include "wsi_platform.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace dxmt {
class VisibilityResultOffsetBumpState {
public:
  void
  beginEncoder() {
    assert(!within_encoder);
    assert(!current_data_is_dirty);
    assert(!~previous_offset);
    within_encoder = true;
  }

  bool
  tryGetNextWriteOffset(bool has_active_occlusion_queries, uint64_t &offset) {
    assert(within_encoder);
    offset = getNextWriteOffset(has_active_occlusion_queries);
    if (offset == previous_offset) {
      return false;
    }
    previous_offset = offset;
    return true;
  }

  uint64_t
  getNextReadOffset() {
    if (within_encoder) {
      if (current_data_is_dirty) {
        current_data_is_dirty = false;
        return ++next_offset;
      }
    }
    assert(!current_data_is_dirty);
    return next_offset;
  }

  void
  endEncoder() {
    assert(within_encoder);
    within_encoder = false;
    previous_offset = ~0uLL;
    if (current_data_is_dirty) {
      next_offset++;
      current_data_is_dirty = false;
    }
  }

  uint64_t
  reset() {
    assert(!within_encoder && "encoder still active");
    assert(!current_data_is_dirty && "encoder still active");
    auto ret = next_offset;
    next_offset = 0;
    return ret;
  }

private:
  uint64_t
  getNextWriteOffset(bool has_active_occlusion_queries) {
    if (has_active_occlusion_queries) {
      current_data_is_dirty = true;
      return next_offset;
    }
    return ~0uLL;
  };

  bool within_encoder = false;
  bool current_data_is_dirty = false;
  uint64_t previous_offset = ~0uLL;
  uint64_t next_offset = 0;
};

class VisibilityResultQuery {
public:
  void
  incRef() {
    refcount_.fetch_add(1u, std::memory_order_acquire);
  }
  void
  decRef() {
    if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
      delete this;
  }

  void
  begin(uint64_t seqId, unsigned offset) {
    accumulated_value_ = 0;
    seq_id_begin = seqId;
    occlusion_counter_begin = offset;
    seq_id_end = ~0uLL;
    occlusion_counter_end = ~0uLL;
  }

  void
  end(uint64_t seqId, unsigned offset) {
    seq_id_end = seqId;
    occlusion_counter_end = offset;
    if (seq_id_begin == seq_id_end && occlusion_counter_begin == occlusion_counter_end) {
      seq_id_issued = seq_id_end;
    }
  }

  uint64_t queryEndAt() {
    return seq_id_end;
  };

  void
  issue(uint64_t seqId, uint64_t const *readbackBuffer, unsigned numResults) {
    assert(seqId >= seq_id_begin);
    assert(seqId <= seq_id_end);
    uint64_t const *start = seqId == seq_id_begin ? readbackBuffer + occlusion_counter_begin : readbackBuffer;
    uint64_t const *end = seqId == seq_id_end ? readbackBuffer + occlusion_counter_end : readbackBuffer + numResults;
    assert(start <= end);
    while (start != end) {
      accumulated_value_ += *start;
      start++;
    }
    seq_id_issued = seqId;
  }

  bool
  getValue(uint64_t *value) {
    if (seq_id_end <= seq_id_issued) {
      *value = accumulated_value_;
      return true;
    }
    return false;
  }

  void
  reset() {
    accumulated_value_ = 0;
    seq_id_begin = ~0uLL;
    occlusion_counter_begin = ~0uLL;
    seq_id_end = ~0uLL;
    occlusion_counter_end = ~0uLL;
    seq_id_issued = 0;
  };

private:
  uint64_t accumulated_value_ = 0;
  uint64_t seq_id_begin = ~0uLL;
  uint64_t occlusion_counter_begin = ~0uLL;
  uint64_t seq_id_end = ~0uLL;
  uint64_t occlusion_counter_end = ~0uLL;
  uint64_t seq_id_issued = 0;
  std::atomic<uint32_t> refcount_ = {0u};
};

class VisibilityResultReadback {
public:
  VisibilityResultReadback(
      WMT::Device device, uint64_t seq_id, uint64_t num_results, std::vector<Rc<VisibilityResultQuery>> &queries
  ) :
      seq_id(seq_id),
      num_results(num_results),
      queries(queries) {
        visibility_result_heap_info.options = WMTResourceHazardTrackingModeUntracked;
        visibility_result_heap_info.memory.set(nullptr);
#ifdef __i386__
        visibility_result_heap_info.memory.set(wsi::aligned_malloc(num_results * sizeof(uint64_t), DXMT_PAGE_SIZE));
#endif
        visibility_result_heap_info.length = num_results * sizeof(uint64_t);
        visibility_result_heap = device.newBuffer(visibility_result_heap_info);
      }
  ~VisibilityResultReadback() {
    for (auto query : queries) {
      query->issue(seq_id, (uint64_t *)visibility_result_heap_info.memory.get(), num_results);
    }
#ifdef __i386__
    wsi::aligned_free(visibility_result_heap_info.memory.get());
#endif
  }

  VisibilityResultReadback(const VisibilityResultReadback &) = delete;
  VisibilityResultReadback(VisibilityResultReadback &&) = delete;

  uint64_t seq_id;
  uint64_t num_results;
  std::vector<Rc<VisibilityResultQuery>> queries;
  WMTBufferInfo visibility_result_heap_info;
  WMT::Reference<WMT::Buffer> visibility_result_heap;
};

/**
 * TODO: rename the whole file to dxmt_query.hpp
 */

class TimestampQuery {
public:
  void
  incRef() {
    refcount_.fetch_add(1u, std::memory_order_acquire);
  }
  void
  decRef() {
    if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
      delete this;
  }

  bool
  getValue(uint64_t *value) {
    if (cached_value_ == ~0ull)
      return false;

    *value = cached_value_;
    return true;
  }

  void
  issue(uint64_t sampled_data) {
    cached_value_ = sampled_data;
  }

private:
  uint64_t cached_value_ = ~0ull;
  std::atomic<uint32_t> refcount_ = {0u};
};

using TimestampQueryList = std::vector<std::pair<Rc<TimestampQuery>, uint64_t>>;

class TimestampReadbackSBuf {
public:
  TimestampReadbackSBuf(
      WMT::Device device, WMT::CommandBuffer cmdbuf, uint64_t num_samples, TimestampQueryList &&queries
  ) :
      queries_(std::move(queries)),
      num_samples_(num_samples) {
    sample_buffer_ = device.newCounterSampleBuffer(num_samples, true);
  }

  ~TimestampReadbackSBuf() {
    // TODO: small_vector opt
    std::vector<uint64_t> results(num_samples_);
    sample_buffer_.resolveCounterRange(0, num_samples_, results.data(), num_samples_ * sizeof(uint64_t));
    for (const auto &[query, sample_index] : queries_) {
      query->issue(results[sample_index]);
    }
  }

  TimestampReadbackSBuf(const TimestampReadbackSBuf &) = delete;
  TimestampReadbackSBuf(TimestampReadbackSBuf &&) = delete;

  WMT::CounterSampleBuffer
  sampleBuffer() {
    return sample_buffer_;
  };

private:
  TimestampQueryList queries_;
  WMT::Reference<WMT::CounterSampleBuffer> sample_buffer_;
  uint64_t num_samples_;
};

class TimestampReadbackCBuf {
public:
  TimestampReadbackCBuf(
      WMT::Device device, WMT::CommandBuffer cmdbuf, uint64_t num_samples, TimestampQueryList &&queries
  ) :
      queries_(std::move(queries)),
      num_samples_(num_samples),
      cmdbuf_(cmdbuf) {}

  ~TimestampReadbackCBuf() {
    // TODO: small_vector opt
    std::vector<uint64_t> results(num_samples_);
    /**
    There is no implicit relationship between `gpuEndTime` and order of commit, but we still want a later issued query
    to return a timestamp greater or equal to previous ones, so check and use maximum.

    `thread_local` makes sense because this destructor is only called on 1. finishing thread 2. (abnormal) device
    destruction
    */
    thread_local uint64_t latest_ts_on_finish_thread = 0;
    latest_ts_on_finish_thread = std::max(cmdbuf_.gpuEndTime(), latest_ts_on_finish_thread);
    std::fill(results.begin(), results.end(), latest_ts_on_finish_thread);
    for (const auto &[query, sample_index] : queries_) {
      query->issue(results[sample_index]);
    }
  }

  TimestampReadbackCBuf(const TimestampReadbackCBuf &) = delete;
  TimestampReadbackCBuf(TimestampReadbackCBuf &&) = delete;

  WMT::CounterSampleBuffer
  sampleBuffer() {
    return {};
  };

private:
  TimestampQueryList queries_;
  uint64_t num_samples_;
  WMT::CommandBuffer cmdbuf_;
};

// compile-time: choose a timestamp impl: SBuf (sample buffer) or CBuf (command buffer `gpuEndTime`)
using TimestampReadback = TimestampReadbackCBuf;

class TimestampQueryState {
public:
  TimestampQueryState(WMT::Device device) : device_(device) {}

  uint64_t
  addQuery(TimestampQuery *query) {
    queries_.push_back({query, num_samples_});
    return num_samples_++;
  }

  void
  coalesceQuery(TimestampQuery *query) {
    assert(num_samples_);
    queries_.push_back({query, num_samples_ - 1});
  }

  std::unique_ptr<TimestampReadback>
  flush(WMT::CommandBuffer cmdbuf) {
    if (num_samples_ == 0)
      return {};

    auto ret = std::make_unique<TimestampReadback>(device_, cmdbuf, num_samples_, std::move(queries_));
    num_samples_ = 0;
    queries_ = {};
    return ret;
  }

private:
  uint64_t num_samples_ = 0;
  TimestampQueryList queries_;
  WMT::Device device_;
};

constexpr unsigned kStreamOutputStreams = 4;

/* The two counters D3D11 keeps per stream: the primitives stream output
   wrote, and those it would have written into buffers that are large enough */
struct StreamOutputCounters {
  uint64_t written = 0;
  uint64_t needed = 0;
};

using StreamOutputCounterSet = std::array<StreamOutputCounters, kStreamOutputStreams>;

/**
 * The counters between a Begin() and an End(). The encoding thread counts the
 * draws whose output it knows and passes its totals to begin() and end(). The
 * draws whose output only the GPU can count write their counters to a buffer
 * of their command buffer, at the positions begin() and end() bracket, and
 * issue() adds them once that command buffer has completed.
 *
 * A command list of a deferred context keeps the query and begins and ends
 * it again each time it executes, so every begin() starts from nothing, and
 * the thread that asks for the range (End() on the immediate context,
 * ExecuteCommandList) calls expect() first: the value is that of the last
 * range asked for, never of one before it.
 */
class StreamOutputQuery {
public:
  void
  incRef() {
    refcount_.fetch_add(1u, std::memory_order_acquire);
  }
  void
  decRef() {
    if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
      delete this;
  }

  /* One more end() has to come before the query has its value */
  void
  expect() {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    expected_++;
  }

  /* False when the query was already open, and only starts over */
  bool
  begin(uint64_t seq_id, const StreamOutputCounterSet &counted, uint32_t gpu_draw) {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    bool opened = !open_;
    open_ = true;
    counted_begin_ = counted;
    counted_ = {};
    gpu_counted_ = {};
    seq_id_begin_ = seq_id;
    gpu_draw_begin_ = gpu_draw;
    seq_id_end_ = ~0uLL;
    seq_id_issued_ = 0;
    return opened;
  }

  /* False when the query was not open: nothing ends */
  bool
  end(uint64_t seq_id, const StreamOutputCounterSet &counted, uint32_t gpu_draw) {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    if (expected_)
      expected_--;
    if (!open_)
      return false;
    open_ = false;
    for (unsigned stream = 0; stream < kStreamOutputStreams; stream++) {
      counted_[stream].written = counted[stream].written - counted_begin_[stream].written;
      counted_[stream].needed = counted[stream].needed - counted_begin_[stream].needed;
    }
    gpu_draw_end_ = gpu_draw;
    seq_id_end_ = seq_id;
    return true;
  }

  /* Whether the range ended in the command buffer seq_id or an earlier one */
  bool
  endedBy(uint64_t seq_id) {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    return seq_id_end_ <= seq_id;
  }

  /* Nobody will end the query or ask for its value any more */
  void
  abandon() {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    abandoned_ = true;
  }

  /* Closes a query that was abandoned while open, which nothing else ends.
     False for every other query. */
  bool
  closeAbandoned() {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    if (!abandoned_ || !open_)
      return false;
    open_ = false;
    return true;
  }

  /* gpu_draws has the counters of each stream for each of the num_gpu_draws
     draws of the command buffer seq_id that the GPU counted. Command buffers
     complete in order, and one gives its counters once. */
  void
  issue(uint64_t seq_id, const StreamOutputCounters *gpu_draws, uint32_t num_gpu_draws) {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    if (seq_id < seq_id_begin_ || seq_id > seq_id_end_ || seq_id <= seq_id_issued_)
      return;
    uint32_t first = seq_id == seq_id_begin_ ? gpu_draw_begin_ : 0;
    uint32_t last = seq_id == seq_id_end_ ? gpu_draw_end_ : num_gpu_draws;
    for (uint32_t draw = first; draw < last && draw < num_gpu_draws; draw++) {
      for (unsigned stream = 0; stream < kStreamOutputStreams; stream++) {
        gpu_counted_[stream].written += gpu_draws[draw * kStreamOutputStreams + stream].written;
        gpu_counted_[stream].needed += gpu_draws[draw * kStreamOutputStreams + stream].needed;
      }
    }
    seq_id_issued_ = seq_id;
  }

  bool
  getValue(StreamOutputCounterSet &value) {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    if (expected_ || seq_id_end_ > seq_id_issued_)
      return false;
    for (unsigned stream = 0; stream < kStreamOutputStreams; stream++) {
      value[stream].written = counted_[stream].written + gpu_counted_[stream].written;
      value[stream].needed = counted_[stream].needed + gpu_counted_[stream].needed;
    }
    return true;
  }

private:
  dxmt::mutex mutex_;
  StreamOutputCounterSet counted_begin_{};
  StreamOutputCounterSet counted_{};
  StreamOutputCounterSet gpu_counted_{};
  uint64_t seq_id_begin_ = ~0uLL;
  uint32_t gpu_draw_begin_ = 0;
  uint32_t gpu_draw_end_ = 0;
  uint64_t seq_id_end_ = ~0uLL;
  uint64_t seq_id_issued_ = 0;
  uint32_t expected_ = 0;
  bool open_ = false;
  bool abandoned_ = false;
  std::atomic<uint32_t> refcount_ = {0u};
};

constexpr uint32_t kStreamOutputReadbackDraws = 256;

/* A buffer the GPU writes the counters of kStreamOutputReadbackDraws draws to,
   or other data of the given length that the CPU reads */
struct StreamOutputReadbackBlock {
  WMTBufferInfo info;
  WMT::Reference<WMT::Buffer> buffer;

  StreamOutputReadbackBlock(
      WMT::Device device,
      uint64_t length = kStreamOutputReadbackDraws * kStreamOutputStreams * sizeof(StreamOutputCounters)
  ) {
    info.options = WMTResourceHazardTrackingModeUntracked;
    info.memory.set(nullptr);
    info.length = length;
#ifdef __i386__
    info.memory.set(wsi::aligned_malloc(info.length, DXMT_PAGE_SIZE));
#endif
    buffer = device.newBuffer(info);
  }
  ~StreamOutputReadbackBlock() {
#ifdef __i386__
    wsi::aligned_free(info.memory.get());
#endif
  }
  StreamOutputReadbackBlock(const StreamOutputReadbackBlock &) = delete;
  StreamOutputReadbackBlock(StreamOutputReadbackBlock &&) = delete;
};

/* Lives until its command buffer has completed, then gives the queries that
   were open in it the counters of that command buffer */
class StreamOutputReadback {
public:
  StreamOutputReadback(
      uint64_t seq_id, std::vector<std::unique_ptr<StreamOutputReadbackBlock>> &&blocks, uint32_t num_gpu_draws,
      const std::vector<Rc<StreamOutputQuery>> &queries
  ) :
      seq_id_(seq_id),
      num_gpu_draws_(num_gpu_draws),
      blocks_(std::move(blocks)),
      queries_(queries) {}

  ~StreamOutputReadback() {
    std::vector<StreamOutputCounters> gpu_draws(size_t(num_gpu_draws_) * kStreamOutputStreams);
    for (uint32_t draw = 0; draw < num_gpu_draws_; draw += kStreamOutputReadbackDraws) {
      auto count = std::min(kStreamOutputReadbackDraws, num_gpu_draws_ - draw);
      memcpy(
          &gpu_draws[size_t(draw) * kStreamOutputStreams], blocks_[draw / kStreamOutputReadbackDraws]->info.memory.get(),
          size_t(count) * kStreamOutputStreams * sizeof(StreamOutputCounters)
      );
    }
    for (auto &query : queries_)
      query->issue(seq_id_, gpu_draws.data(), num_gpu_draws_);
  }

  StreamOutputReadback(const StreamOutputReadback &) = delete;
  StreamOutputReadback(StreamOutputReadback &&) = delete;

private:
  uint64_t seq_id_;
  uint32_t num_gpu_draws_;
  std::vector<std::unique_ptr<StreamOutputReadbackBlock>> blocks_;
  std::vector<Rc<StreamOutputQuery>> queries_;
};

struct QueryReadbacks {
  std::unique_ptr<VisibilityResultReadback> visibility;
  std::unique_ptr<TimestampReadback> timestamp;
  std::unique_ptr<StreamOutputReadback> stream_output;
};

} // namespace dxmt
