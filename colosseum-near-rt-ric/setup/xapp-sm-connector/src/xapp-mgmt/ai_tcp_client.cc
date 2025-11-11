#include "ai_tcp_client.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

extern "C" {
#include "mdclog/mdclog.h"
}

AiTcpClient::AiTcpClient(const std::string& host, int port)
    : host_(host),
      port_(port),
      sock_(-1)
{
}

AiTcpClient::~AiTcpClient() {
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

static std::string toHex(const std::string& data)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (unsigned char c : data) {
        out.push_back(kHex[(c >> 4) & 0xF]);
        out.push_back(kHex[c & 0xF]);
    }
    return out;
}

bool AiTcpClient::SendKpi(const std::string& meid,
                          const std::string& kpi_json)
{
    // KPI may be binary or arbitrary bytes; send as hex string for robustness.
    // Schema: {"type":"kpi","meid":"...","kpi_hex":"<hex-bytes>"}
    std::string kpi_hex = toHex(kpi_json);
    std::string msg = "{\"type\":\"kpi\",\"meid\":\"" + meid + "\",\"kpi_hex\":\"" + kpi_hex + "\"}";

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
    // Request:
    // {"type":"recommendation_request","meid":"...","kpi_hex":"<hex-bytes>"}
    std::string kpi_hex = toHex(kpi_json);
    std::string req = "{\"type\":\"recommendation_request\",\"meid\":\"" + meid +
                      "\",\"kpi_hex\":\"" + kpi_hex + "\"}";

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
        return true;
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
    mdclog_write(MDCLOG_INFO, "[AI-TCP] Connected to AI at %s:%d",
                 host_.c_str(), port_);
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
        mdclog_write(MDCLOG_INFO, "[AI-TCP] Closing AI connection");
        close(sock_);
        sock_ = -1;
    }
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
