/**
 * @file x11_scroll_baseline.cpp
 * @brief Send zero relative scroll valuators before the real uinput wheel report.
 */
#include "x11_scroll_baseline.hpp"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/uio.h>
#include <utility>
#include <X11/Xlib-xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xinput.h>
#include <xcb/xtest.h>

namespace lvh::detail {
  namespace {
    /** @brief Own an XCB reply or error allocated by the C API. */
    template<class T>
    using reply_ptr = std::unique_ptr<T, decltype(&std::free)>;

    /** @brief Match the device node, never a shared or user-controlled device name. */
    bool owns_device(xcb_connection_t *connection, uint16_t id, xcb_atom_t property, const std::vector<std::string> &nodes) {
      xcb_generic_error_t *raw_error = nullptr;
      reply_ptr<xcb_input_xi_get_property_reply_t> reply(
        xcb_input_xi_get_property_reply(connection, xcb_input_xi_get_property(connection, id, 0, property, XCB_ATOM_STRING, 0, 1024), &raw_error),
        &std::free
      );
      reply_ptr<xcb_generic_error_t> error(raw_error, &std::free);
      if (error || !reply || reply->type != XCB_ATOM_STRING || reply->format != 8 || reply->bytes_after) {
        return false;
      }
      const auto *value = static_cast<const char *>(xcb_input_xi_get_property_items(reply.get()));
      const std::string_view node(value, reply->num_items);
      return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
    }
  }  // namespace

  /** @brief Resolve the owned pointer and deliver ordered XTEST device events. */
  static bool submit_pointer(Display *display, const std::vector<std::string> &event_nodes, std::optional<std::pair<int, int>> position, bool pixel_relative = false) {
    if (!display || event_nodes.empty()) {
      return false;
    }
    // Share the absolute-motion connection so the X server cannot reorder an
    // earlier XTEST move after this baseline. XCB owns these individual replies;
    // it does not take over Xlib's event queue or process-wide error handler.
    XFlush(display);
    auto *connection = XGetXCBConnection(display);
    if (!connection || xcb_connection_has_error(connection)) {
      return false;
    }
    const auto *input = xcb_get_extension_data(connection, &xcb_input_id);
    const auto *test = xcb_get_extension_data(connection, &xcb_test_id);
    if (!input || !input->present || !test || !test->present) {
      return false;
    }
    xcb_generic_error_t *raw_error = nullptr;
    reply_ptr<xcb_input_xi_query_version_reply_t> version(
      xcb_input_xi_query_version_reply(connection, xcb_input_xi_query_version(connection, 2, 1), &raw_error),
      &std::free
    );
    reply_ptr<xcb_generic_error_t> version_error(raw_error, &std::free);
    if (version_error || !version || version->major_version < 2 || (version->major_version == 2 && version->minor_version < 1)) {
      return false;
    }
    reply_ptr<xcb_intern_atom_reply_t> atom(
      xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 1, 11, "Device Node"), nullptr),
      &std::free
    );
    if (!atom || atom->atom == XCB_ATOM_NONE) {
      return false;
    }
    raw_error = nullptr;
    reply_ptr<xcb_input_xi_query_device_reply_t> devices(
      xcb_input_xi_query_device_reply(connection, xcb_input_xi_query_device(connection, XCB_INPUT_DEVICE_ALL), &raw_error),
      &std::free
    );
    reply_ptr<xcb_generic_error_t> device_error(raw_error, &std::free);
    if (device_error || !devices) {
      return false;
    }
    for (auto it = xcb_input_xi_query_device_infos_iterator(devices.get()); it.rem;
         xcb_input_xi_device_info_next(&it)) {
      const auto &device = *it.data;
      // XTEST's XI1 event encoding reserves bit 7 for MORE_EVENTS. Never
      // truncate an XI2 id into a different device; unsupported layouts fail soft.
      if (!device.enabled || device.type != XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER || device.attachment == 0 || device.deviceid > 127 || !owns_device(connection, device.deviceid, atom->atom, event_nodes)) {
        continue;
      }
      int first = 256, last = -1;
      for (auto c = xcb_input_xi_device_info_classes_iterator(&device); c.rem;
           xcb_input_device_class_next(&c)) {
        if (c.data->type != XCB_INPUT_DEVICE_CLASS_TYPE_SCROLL) {
          continue;
        }
        const auto &scroll = *reinterpret_cast<xcb_input_scroll_class_t *>(c.data);
        first = std::min(first, int(scroll.number));
        last = std::max(last, int(scroll.number));
      }
      if (last < first || first < 2 || last > 255 || last - first >= 6) {
        return false;
      }
      // Only relative axes may be primed: zero must mean no displacement.
      int relative = 0;
      for (auto c = xcb_input_xi_device_info_classes_iterator(&device); c.rem;
           xcb_input_device_class_next(&c)) {
        if (c.data->type != XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR) {
          continue;
        }
        const auto &axis = *reinterpret_cast<xcb_input_valuator_class_t *>(c.data);
        if (axis.number >= first && axis.number <= last && axis.mode == XCB_INPUT_VALUATOR_MODE_RELATIVE) {
          ++relative;
        }
      }
      if (relative != last - first + 1) {
        return false;
      }

      // libXtst's XTestFakeDeviceMotionEvent encoding, using a checked XCB
      // request so hot-unplug/BadDevice cannot terminate the media worker.
      struct Packet {
        xcb_test_fake_input_request_t input;
        xcb_input_device_valuator_event_t valuators;
      } packet {};

      static_assert(sizeof(Packet) == 68);
      packet.input.type = input->first_event + XCB_INPUT_DEVICE_MOTION_NOTIFY;
      packet.input.detail = 1;  // relative, including the cumulative scroll axes
      packet.input.deviceid = uint8_t(device.deviceid) | 0x80;
      packet.valuators.response_type = input->first_event + XCB_INPUT_DEVICE_VALUATOR;
      packet.valuators.device_id = uint8_t(device.deviceid);
      packet.valuators.first_valuator = uint8_t(first);
      packet.valuators.num_valuators = uint8_t(last - first + 1);
      const auto send = [&](Packet &value) {
        const xcb_protocol_request_t request {2, &xcb_test_id, XCB_TEST_FAKE_INPUT, 1};
        iovec parts[4] {};
        parts[2] = {&value, sizeof(value)};
        parts[3] = {nullptr, 0};
        const xcb_void_cookie_t cookie {xcb_send_request(connection, XCB_REQUEST_CHECKED, parts + 2, &request)};
        reply_ptr<xcb_generic_error_t> error(xcb_request_check(connection, cookie), &std::free);
        return cookie.sequence && !error && !xcb_connection_has_error(connection);
      };
      if (position) {
        auto motion = packet;
        // XTEST relative device motion uses desktop-pixel offsets without
        // POINTER_ACCELERATE; ordinary uinput REL_X/REL_Y retains acceleration.
        motion.input.detail = pixel_relative ? 1 : 0;
        motion.valuators.first_valuator = 0;
        motion.valuators.num_valuators = 2;
        motion.valuators.valuators[0] = position->first;
        motion.valuators.valuators[1] = position->second;
        if (!send(motion)) {
          return false;
        }
      }
      // Qt refreshes scroll state when the source changes. Moving the owned
      // pointer avoids that refresh at every wheel, including its query race.
      // GTK also needs the zero baseline after motion/entry; no distance is added.
      return send(packet);
    }
    return false;
  }

  bool prime_x11_scroll(Display *display, const std::vector<std::string> &event_nodes) {
    return submit_pointer(display, event_nodes, std::nullopt);
  }

  bool move_x11_pointer(Display *display, const std::vector<std::string> &event_nodes, int x, int y) {
    return submit_pointer(display, event_nodes, std::pair {x, y});
  }

  bool move_x11_pointer_pixels(Display *display, const std::vector<std::string> &event_nodes, int delta_x, int delta_y) {
    return submit_pointer(display, event_nodes, std::pair {delta_x, delta_y}, true);
  }

}  // namespace lvh::detail
