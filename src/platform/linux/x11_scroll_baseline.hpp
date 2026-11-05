/**
 * @file x11_scroll_baseline.hpp
 * @brief Preserve X11 smooth-scroll baselines across pointer source changes.
 */
#pragma once
#include <string>
#include <vector>
#include <X11/Xlib.h>

namespace lvh::detail {
  /**
   * @brief Prime the owned evdev pointer's cumulative scroll axes without scrolling.
   * @param display The same connection used for absolute XTEST motion.
   * @param event_nodes Exact input-event paths owned by the virtual mouse.
   * @return True if the baseline was delivered before subsequent evdev input.
   */
  bool prime_x11_scroll(Display *display, const std::vector<std::string> &event_nodes);

  /**
   * @brief Position the owned XInput pointer without switching to the core XTEST source.
   * @param display Absolute-motion connection.
   * @param event_nodes Owned mouse event node paths.
   * @param x Desktop pixel X coordinate.
   * @param y Desktop pixel Y coordinate.
   * @return True when both positioning and scroll baseline requests succeeded.
   */
  bool move_x11_pointer(Display *display, const std::vector<std::string> &event_nodes, int x, int y);
}  // namespace lvh::detail
