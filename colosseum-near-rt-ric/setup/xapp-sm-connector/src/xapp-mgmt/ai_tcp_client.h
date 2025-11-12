#pragma once

#include <string>
#include <mutex>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

// Thin client used by msgs_proc.cc to talk to the external AI over TCP.
//
// Protocol (default suggestion):
// - Length-prefixed frames: [uint32 len in network byte order][JSON bytes]
// - KPI message:
//     {"type":"kpi","meid":"...","kpi":{...}}
// - Recommendation request:
//     {"type":"recommendation_request","meid":"...","kpi":{...}}
// - Recommendation reply (convention used here):
//     - empty / "{}" / contains "no_action"  => no action
//     - otherwise: body is the exact command JSON to send to ns-3
//
class AiTcpClient {
public:
    // Does NOT connect immediately; connection is established on first use.
    AiTcpClient(const std::string& host, int port);
    ~AiTcpClient();

    // Best-effort, fire-and-forget KPI publish.
    // Returns true on successful send, false otherwise.
    bool SendKpi(const std::string& meid,
                 const std::string& kpi_json);

    // Synchronous request/response:
    // - Sends KPI/context to AI
    // - If AI returns a command, writes JSON into out_cmd_json and returns true.
    // - If no action or error, returns false.
    bool GetRecommendation(const std::string& meid,
                           const std::string& kpi_json,
                           std::string& out_cmd_json);
    
    // Listen for unsolicited config messages from AI (non-blocking, runs in background)
    // When AI sends a config, calls handler(config_json)
    void StartConfigListener(std::function<bool(const std::string&)> handler);
    void StopConfigListener();

private:
    bool ensureConnected();
    bool sendFramed(const std::string& json);
    bool sendAll(const void* buf, size_t len);
    bool recvAll(void* buf, size_t len);
    void reset();

    std::string host_;
    int         port_;
    int         sock_;
    std::mutex mtx_;
    
    // Config listener (for bidirectional communication)
    std::function<bool(const std::string&)> config_handler_;
    std::atomic<bool> listener_running_;
    std::unique_ptr<std::thread> listener_thread_;
    void configListenerLoop();
};

// Global accessor used by msgs_proc.cc so we don't pass instances around.
AiTcpClient& GetAiTcpClient();
