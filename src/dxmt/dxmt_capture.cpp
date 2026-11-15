#include "dxmt_capture.hpp"
#include "log/log.hpp"
#include "util_env.hpp"

#ifdef __WIN32
#include "windows.h"
#endif

namespace dxmt {

CaptureState::CaptureState() {
  if (env::getEnvVar("MTL_CAPTURE_ENABLED") != "1")
    return;
  if (env::getExeBaseName() != env::getEnvVar("DXMT_CAPTURE_EXECUTABLE"))
    return;
  WARN("DXMT capture enabled");
  state = State::Idle;
}

CaptureState::NextAction
CaptureState::getNextAction(uint64_t frame) {
  switch (state) {
  case State::Freezed:
    return NextAction::Nothing;
  case State::Idle:
    break;
  case State::WillCapture:
    if (frame == next_capture_frame) {
      state = State::Capturing;
      return NextAction::StartCapture;
    }
    break;
  case State::Capturing:
    if (frame >= next_capture_frame + kCaptureFrames) {
      next_capture_frame = 0;
      state = State::Idle;
      return NextAction::StopCapture;
    }
    break;
  }
  return NextAction::Nothing;
};

void
CaptureState::scheduleNextFrameCapture(uint64_t next) {
  if (state != State::Idle)
    return;
  state = State::WillCapture;
  next_capture_frame = next;
}

bool
CaptureState::shouldCaptureNextFrame(uint64_t frame) {
#ifdef _WIN32
  /* Without capture nothing reads the keyboard. */
  if (state == State::Freezed)
    return false;
  /* Ctrl+Shift+P: P going down while Control and Shift are held and Alt is
   * not, sampled once per frame (the first chunk of the frame), as Wine's
   * Vulkan present and mtld3d sample it. */
  if (frame == sampled_frame_)
    return false;
  sampled_frame_ = frame;
  bool p_down = GetAsyncKeyState('P') & 0x8000;
  bool pressed = p_down && !capture_key_pressed_ &&
                 (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                 (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                 !(GetAsyncKeyState(VK_MENU) & 0x8000);
  capture_key_pressed_ = p_down;
  return pressed && state == State::Idle;
#endif
  return false;
};

} // namespace dxmt
