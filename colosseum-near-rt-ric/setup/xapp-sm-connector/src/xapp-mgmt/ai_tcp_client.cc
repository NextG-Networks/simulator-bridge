#include "ai_tcp_client.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

extern "C" {
#include "mdclog/mdclog.h"
}

AiTcpClient::AiTcpClient(const std::string& host, int port)
    : host_(host),
      port_(port),
      sock_(-1),
      listener_running_(false),
      control_listener_running_(false)
{
}

AiTcpClient::~AiTcpClient() {
    StopControlCommandListener();
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

bool AiTcpClient::SendKpi(const std::string& meid,
                          const std::string& kpi_json)
{
    // Send decoded JSON directly (kpi_json is already a decoded E2SM JSON object)
    // Schema: {"type":"kpi","meid":"...","kpi":{...decoded JSON...}}
    // kpi_json is already a valid JSON object, so embed it directly
    std::string msg = "{\"type\":\"kpi\",\"meid\":\"" + meid + "\",\"kpi\":" + kpi_json + "}";

    std::lock_guard<std::mutex> lock(mtx_);
    if (!ensureConnected()) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to connect when sending KPI (MEID=%s)",
                     meid.c_str());
        return false;
    }

    if (!sendFramed(msg)) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to send KPI frame (MEID=%s)", meid.c_str());
        reset();
        return false;
    }

    mdclog_write(MDCLOG_DEBUG, "[AI-TCP] Sent KPI (MEID=%s, bytes=%zu)",
                 meid.c_str(), msg.size());
    return true;
}

bool AiTcpClient::GetRecommendation(const std::string& meid,
                                    const std::string& kpi_json,
                                    std::string& out_cmd_json)
{
    // Request: send decoded JSON directly
    // {"type":"recommendation_request","meid":"...","kpi":{...decoded JSON...}}
    std::string req = "{\"type\":\"recommendation_request\",\"meid\":\"" + meid +
                      "\",\"kpi\":" + kpi_json + "}";

    std::lock_guard<std::mutex> lock(mtx_);
    if (!ensureConnected()) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to connect for recommendation (MEID=%s)",
                     meid.c_str());
        return false;
    }

    if (!sendFramed(req)) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to send recommendation_request (MEID=%s)",
                     meid.c_str());
        reset();
        return false;
    }

    // Read reply frame
    uint32_t len_net = 0;
    if (!recvAll(&len_net, sizeof(len_net))) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to read reply length (MEID=%s)",
                     meid.c_str());
        reset();
        return false;
    }

    uint32_t len = ntohl(len_net);
    if (len == 0 || len > 1024 * 1024) { // 1MB sanity cap
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Invalid reply length=%u (MEID=%s)",
                     len, meid.c_str());
        reset();
        return false;
    }

    std::string reply(len, '\0');
    if (!recvAll(&reply[0], len)) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] Failed to read reply body (MEID=%s)",
                     meid.c_str());
        reset();
        return false;
    }

    // Trim whitespace
    auto trim = [](std::string& s) {
        const char* ws = " \t\r\n";
        auto start = s.find_first_not_of(ws);
        auto end   = s.find_last_not_of(ws);
        if (start == std::string::npos) { s.clear(); return; }
        s = s.substr(start, end - start + 1);
    };

    trim(reply);

    // Convention:
    // - empty / "{}" / contains "no_action" => no command
    if (reply.empty() ||
        reply == "{}" ||
        reply.find("no_action") != std::string::npos)
    {
        mdclog_write(MDCLOG_DEBUG,
                     "[AI-TCP] No action in reply from AI (MEID=%s, raw=\"%s\")",
                     meid.c_str(), reply.c_str());
        return false;
    }

    // Otherwise: reply is the exact command JSON to send to ns-3.
    out_cmd_json = reply;

    mdclog_write(MDCLOG_INFO,
                 "[AI-TCP] Got recommendation for MEID=%s: %s",
                 meid.c_str(), out_cmd_json.c_str());
    return true;
}

bool AiTcpClient::ensureConnected() {
    if (sock_ >= 0) {
        // Verify socket is still valid by checking if it's still connected
        // (simple check - try to get socket error)
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
            return true;
        } else {
            // Socket is broken, reset it
            mdclog_write(MDCLOG_WARN, "[AI-TCP] Socket appears broken, resetting connection");
            reset();
        }
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] socket() failed: %s", strerror(errno));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] inet_pton(%s) failed", host_.c_str());
        close(s);
        return false;
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-TCP] connect(%s:%d) failed: %s",
                     host_.c_str(), port_, strerror(errno));
        close(s);
        return false;
    }

    sock_ = s;
    mdclog_write(MDCLOG_INFO, "[AI-TCP] Connected to AI at %s:%d (socket=%d)",
                 host_.c_str(), port_, sock_);
    
    // Notify listener that socket is now available
    if (listener_running_) {
        mdclog_write(MDCLOG_INFO, "[AI-TCP] Socket connected - listener can now receive messages");
    }
    
    return true;
}

bool AiTcpClient::sendFramed(const std::string& json) {
    uint32_t len = static_cast<uint32_t>(json.size());
    uint32_t len_net = htonl(len);

    if (!sendAll(&len_net, sizeof(len_net))) {
        return false;
    }
    if (!sendAll(json.data(), len)) {
        return false;
    }
    return true;
}

bool AiTcpClient::sendAll(const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = send(sock_, p, len, 0);
        if (n <= 0) {
            mdclog_write(MDCLOG_ERR, "[AI-TCP] send() failed: %s", strerror(errno));
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool AiTcpClient::recvAll(void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = recv(sock_, p, len, 0);
        if (n <= 0) {
            mdclog_write(MDCLOG_ERR, "[AI-TCP] recv() failed: %s", strerror(errno));
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void AiTcpClient::reset() {
    if (sock_ >= 0) {
        mdclog_write(MDCLOG_INFO, "[AI-TCP] Closing AI connection (socket=%d)", sock_);
        close(sock_);
        sock_ = -1;
        mdclog_write(MDCLOG_DEBUG, "[AI-TCP] Connection reset - listener will wait for reconnection");
    }
}

// Config listener methods removed - config file writing is no longer supported.
// All control should go through the direct RIC control path via StartControlCommandListener().

void AiTcpClient::StartControlCommandListener(std::function<bool(const std::string&, const std::string&)> handler) {
    bool need_connect = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        control_cmd_handler_ = std::move(handler);
        control_listener_running_ = true;
        mdclog_write(MDCLOG_INFO, "[AI-TCP] Control command handler installed");
        
        // Check if we need to connect (check inside lock for thread safety)
        need_connect = (sock_ < 0);
    }
    
    // Try to establish connection proactively so listener can receive commands
    // even before KPIs are sent (call outside of lock since ensureConnected locks)
    if (need_connect) {
        mdclog_write(MDCLOG_INFO, "[AI-TCP] Attempting to connect to AI server at %s:%d for control commands...", 
                     host_.c_str(), port_);
        ensureConnected();
        // Note: ensureConnected will log success/failure
    }
    
    // If the main listener isn't running yet, start it
    // (it will handle control commands)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!listener_running_) {
            listener_running_ = true;
            listener_thread_ = std::unique_ptr<std::thread>(new std::thread(&AiTcpClient::configListenerLoop, this));
            mdclog_write(MDCLOG_INFO, "[AI-TCP] Started listener thread for control commands");
        } else {
            mdclog_write(MDCLOG_INFO, "[AI-TCP] Listener thread already running, handler installed");
        }
    }
}

void AiTcpClient::StopControlCommandListener() {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (!control_listener_running_) {
        return;
    }
    
    control_listener_running_ = false;
    control_cmd_handler_ = nullptr;
    
    // Note: We don't stop the listener thread here because it might be needed for other purposes.
    // The listener will continue running if listener_running_ is still true.
    
    mdclog_write(MDCLOG_INFO, "[AI-TCP] Control command handler removed (listener thread continues running)");
}

void AiTcpClient::configListenerLoop() {
    mdclog_write(MDCLOG_INFO, "[AI-TCP] Listener loop started (waiting for connection...)");
    int consecutive_no_connection = 0;
    while (listener_running_) {
        int current_sock = -1;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            current_sock = sock_;
        }
        
        // Sleep outside of lock if not connected
        if (current_sock < 0) {
            consecutive_no_connection++;
            // Try to connect every 10 seconds (100 iterations * 100ms)
            if (consecutive_no_connection % 100 == 0) {
                // Try to establish connection
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    if (sock_ < 0) {
                        mdclog_write(MDCLOG_INFO, "[AI-TCP] Attempting to connect to AI server at %s:%d...", 
                                     host_.c_str(), port_);
                        // Unlock before calling ensureConnected (it will lock again)
                    }
                }
                // Call ensureConnected without holding the lock
                ensureConnected();
            }
            // Log every 50 iterations (5 seconds) that we're waiting (but only at DEBUG level)
            if (consecutive_no_connection % 50 == 0 && consecutive_no_connection <= 200) {
                // Only log first few times to avoid spam
                mdclog_write(MDCLOG_DEBUG, "[AI-TCP] Listener waiting for socket connection (waited %d seconds)...", 
                            consecutive_no_connection / 10);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Reset counter when connected
        if (consecutive_no_connection > 0) {
            mdclog_write(MDCLOG_INFO, "[AI-TCP] Listener detected socket connection (socket=%d)", current_sock);
            consecutive_no_connection = 0;
        }
        
        // Poll outside of lock to avoid blocking other operations
        pollfd pfd{};
        pfd.fd = current_sock;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 100); // 100ms timeout
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            // Data available, try to read a config message
            mdclog_write(MDCLOG_DEBUG, "[AI-TCP] Data available on socket, reading message...");
            std::lock_guard<std::mutex> lock(mtx_);
            uint32_t len_net = 0;
            if (recvAll(&len_net, sizeof(len_net))) {
                uint32_t len = ntohl(len_net);
                if (len > 0 && len <= 1024 * 1024) {
                    std::string config_json(len, '\0');
                    if (recvAll(&config_json[0], len)) {
                        mdclog_write(MDCLOG_DEBUG, "[AI-TCP] Received message from AI (len=%u): %s", 
                                    len, config_json.substr(0, 200).c_str());
                        // Check if it's a control command message
                        if (config_json.find("\"type\":\"control\"") != std::string::npos || 
                            config_json.find("\"type\": \"control\"") != std::string::npos) {
                            mdclog_write(MDCLOG_INFO, "[AI-TCP] ✅ Detected control command message (len=%u)", len);
                            // This is a control command - parse and route to control command handler
                            if (control_cmd_handler_) {
                                // Parse JSON to extract meid and cmd using rapidjson
                                // Expected format: {"type":"control","meid":"...","cmd":{...}}
                                // or: {"type":"control","meid":"...","command":{...}}
                                rapidjson::Document doc;
                                if (doc.Parse(config_json.c_str()).HasParseError()) {
                                    mdclog_write(MDCLOG_WARN, "[AI-TCP] Failed to parse control command JSON: %s", 
                                                config_json.c_str());
                                    continue;
                                }
                                
                                if (!doc.IsObject()) {
                                    mdclog_write(MDCLOG_WARN, "[AI-TCP] Control command is not a JSON object: %s", 
                                                config_json.c_str());
                                    continue;
                                }
                                
                                // Extract meid
                                std::string meid;
                                if (doc.HasMember("meid") && doc["meid"].IsString()) {
                                    meid = doc["meid"].GetString();
                                }
                                
                                // Extract cmd (try "cmd" first, then "command")
                                std::string cmd_json;
                                if (doc.HasMember("cmd")) {
                                    if (doc["cmd"].IsString()) {
                                        // If cmd is a string, use it directly
                                        cmd_json = doc["cmd"].GetString();
                                        mdclog_write(MDCLOG_INFO, "[AI-TCP] cmd is a string: '%s'", cmd_json.c_str());
                                    } else if (doc["cmd"].IsObject()) {
                                        // If cmd is an object, serialize it
                                        rapidjson::StringBuffer buffer;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                                        doc["cmd"].Accept(writer);
                                        cmd_json = buffer.GetString();
                                        mdclog_write(MDCLOG_INFO, "[AI-TCP] cmd is an object, serialized to: '%s'", cmd_json.c_str());
                                    } else {
                                        mdclog_write(MDCLOG_WARN, "[AI-TCP] cmd field exists but is neither string nor object (type=%d)", doc["cmd"].GetType());
                                    }
                                } else if (doc.HasMember("command")) {
                                    if (doc["command"].IsString()) {
                                        // If command is a string, use it directly
                                        cmd_json = doc["command"].GetString();
                                        mdclog_write(MDCLOG_INFO, "[AI-TCP] command is a string: '%s'", cmd_json.c_str());
                                    } else if (doc["command"].IsObject()) {
                                        // If command is an object, serialize it
                                        rapidjson::StringBuffer buffer;
                                        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                                        doc["command"].Accept(writer);
                                        cmd_json = buffer.GetString();
                                        mdclog_write(MDCLOG_INFO, "[AI-TCP] command is an object, serialized to: '%s'", cmd_json.c_str());
                                    } else {
                                        mdclog_write(MDCLOG_WARN, "[AI-TCP] command field exists but is neither string nor object");
                                    }
                                } else {
                                    mdclog_write(MDCLOG_WARN, "[AI-TCP] No 'cmd' or 'command' field found in control message");
                                }
                                
                                if (!meid.empty() && !cmd_json.empty()) {
                                    mdclog_write(MDCLOG_INFO, "[AI-TCP] ✅ Extracted control command: meid='%s', cmd_json='%s' (len=%zu)", 
                                                meid.c_str(), cmd_json.c_str(), cmd_json.size());
                                    mdclog_write(MDCLOG_INFO, "[AI-TCP] → Forwarding to send_ctrl_ callback...");
                                    control_cmd_handler_(meid, cmd_json);
                                    mdclog_write(MDCLOG_INFO, "[AI-TCP] ✅ send_ctrl_ callback returned");
                                } else {
                                    mdclog_write(MDCLOG_WARN, "[AI-TCP] ❌ Received control command but missing meid or cmd: meid='%s' (len=%zu), cmd='%s' (len=%zu)", 
                                                meid.c_str(), meid.size(), cmd_json.c_str(), cmd_json.size());
                                }
                            }
                            continue;
                        }
                        
                        // Config messages (qos, handover, energy, etc.) are received but not written to CSV files.
                        // The xApp can still receive these JSONs from the AI, but they are logged and ignored.
                        // All control should go through the direct RIC control path with "type":"control".
                        if (config_json.find("\"type\":\"config\"") != std::string::npos ||
                            config_json.find("\"type\":\"qos\"") != std::string::npos ||
                            config_json.find("\"type\":\"handover\"") != std::string::npos ||
                            config_json.find("\"type\":\"energy\"") != std::string::npos) {
                            mdclog_write(MDCLOG_INFO, "[AI-TCP] Received config message (CSV file writing disabled). Use direct RIC control with \"type\":\"control\" instead: %s", 
                                        config_json.substr(0, 200).c_str());
                            continue;
                        }
                        // Otherwise it might be a recommendation response, but we can't handle it here
                        // since GetRecommendation expects it. This is a limitation of the current design.
                    }
                }
            } else {
                // Connection lost, reset
                mdclog_write(MDCLOG_WARN, "[AI-TCP] Failed to read message length, resetting connection");
                reset();
            }
        } else if (ret < 0) {
            // Error
            mdclog_write(MDCLOG_WARN, "[AI-TCP] Poll error, resetting connection");
            reset();
        }
        // If ret == 0, timeout - just continue loop
    }
    
    mdclog_write(MDCLOG_INFO, "[AI-TCP] Listener loop exited");
}

// Global singleton with env-configurable host/port
AiTcpClient& GetAiTcpClient() {
    static std::string host = [] {
        const char* h = std::getenv("AI_HOST");
        return h ? std::string(h) : std::string("127.0.0.1");
    }();

    static int port = [] {
        const char* p = std::getenv("AI_PORT");
        return p ? std::atoi(p) : 5000;
    }();

    static AiTcpClient client(host, port);
    return client;
}
