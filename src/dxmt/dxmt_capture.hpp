#pragma once
#include <cstdint>

namespace dxmt {

class CaptureState {
public:
  enum class NextAction {
    StartCapture,
    StopCapture,
    Nothing,
  };
  NextAction getNextAction(uint64_t frame);
  void scheduleNextFrameCapture(uint64_t next = 0);
  bool shouldCaptureNextFrame(uint64_t frame);
  /* The number of the capture that starts now, counted from 1. */
  uint32_t nextCaptureIndex() { return ++capture_count_; }

  /* The frames a capture records, as Wine's and mtld3d's capture chord. */
  static constexpr uint64_t kCaptureFrames = 3;

  CaptureState();

private:
  enum class State {
    Freezed,
    Idle,
    WillCapture,
    Capturing,
  };
  State state = State::Freezed;
  uint64_t next_capture_frame = 0;
  bool capture_key_pressed_ = false;
  uint32_t capture_count_ = 0;
  uint64_t sampled_frame_ = ~0ull;
};

} // namespace dxmt
