#pragma once
#include <functional>
#include <string>
#include "ns3/simulator.h"
#include "ns3/log.h"

namespace ns3 {

/**
 * Minimal global handoff from E2 decoder to scenario code.
 * Scenario sets a handler; E2 side calls Handle() with the raw ASCII control.
 */
class ControlGateway
{
public:
  static void SetHandler(std::function<void(const std::string&)> h) { s_handler = std::move(h); }
  static bool HasHandler() { return (bool)s_handler; }

  // Safe to call from any thread; runs handler on the ns-3 event loop immediately.
  static void Handle(const std::string& ascii)
  {
    if (!s_handler) return;
    std::string copy = ascii; // capture by value for safety
    Simulator::ScheduleNow(&ControlGateway::DoHandle, copy);
  }

private:
  static void DoHandle(std::string ascii)
  {
    if (s_handler) s_handler(ascii);
  }

  static std::function<void(const std::string&)> s_handler;
};

inline std::function<void(const std::string&)> ControlGateway::s_handler = nullptr;

} // namespace ns3
