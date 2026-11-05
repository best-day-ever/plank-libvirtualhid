/**
 * @file x11_scroll_baseline.hpp
 * @brief Preserve X11 smooth-scroll baselines across pointer source changes.
 */
#pragma once
#include <X11/Xlib.h>
#include <string>
#include <vector>

namespace lvh::detail {
  /**
   * @brief Prime the owned evdev pointer's cumulative scroll axes without scrolling.
   * @param display The same connection used for absolute XTEST motion.
   * @param event_nodes Exact input-event paths owned by the virtual mouse.
   * @return True if the baseline was delivered before subsequent evdev input.
   */
  bool prime_x11_scroll(Display *display, const std::vector<std::string> &event_nodes);
}
