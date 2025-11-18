#include "ai_config_receiver.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

extern "C" {
#include "mdclog/mdclog.h"
}

AiConfigReceiver::AiConfigReceiver(int port, ConfigHandler handler)
    : port_(port),
      listen_fd_(-1),
      handler_(std::move(handler)),
      running_(false)
{
}

AiConfigReceiver::~AiConfigReceiver() {
    Stop();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void AiConfigReceiver::Start() {
    if (running_) {
        mdclog_write(MDCLOG_WARN, "[AI-CONFIG] Already running");
        return;
    }
    
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] socket() failed: %s", strerror(errno));
        return;
    }
    
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] setsockopt() failed: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] bind(%d) failed: %s", port_, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    
    if (listen(listen_fd_, 5) < 0) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] listen() failed: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    
    running_ = true;
    server_thread_ = std::unique_ptr<std::thread>(new std::thread(&AiConfigReceiver::run, this));
    
    mdclog_write(MDCLOG_INFO, "[AI-CONFIG] Listening on port %d", port_);
}

void AiConfigReceiver::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
    }
    
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    
    mdclog_write(MDCLOG_INFO, "[AI-CONFIG] Stopped");
}

void AiConfigReceiver::run() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        
        if (client_fd < 0) {
            if (running_) {
                mdclog_write(MDCLOG_ERR, "[AI-CONFIG] accept() failed: %s", strerror(errno));
            }
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        
        mdclog_write(MDCLOG_INFO, "[AI-CONFIG] Accepted connection from %s:%d", 
                    client_ip, ntohs(client_addr.sin_port));
        
        if (handleConnection(client_fd)) {
            mdclog_write(MDCLOG_INFO, "[AI-CONFIG] Successfully processed config from %s", client_ip);
        } else {
            mdclog_write(MDCLOG_WARN, "[AI-CONFIG] Failed to process config from %s", client_ip);
        }
        
        close(client_fd);
    }
}

bool AiConfigReceiver::handleConnection(int client_fd) {
    std::string config_json;
    if (!recvFramed(client_fd, config_json)) {
        return false;
    }
    
    if (config_json.empty()) {
        mdclog_write(MDCLOG_WARN, "[AI-CONFIG] Received empty config");
        return false;
    }
    
    mdclog_write(MDCLOG_DEBUG, "[AI-CONFIG] Received config: %s", config_json.c_str());
    
    if (handler_) {
        return handler_(config_json);
    }
    
    return false;
}

bool AiConfigReceiver::recvFramed(int fd, std::string& out_json) {
    uint32_t len_net = 0;
    ssize_t n = recv(fd, &len_net, sizeof(len_net), MSG_WAITALL);
    if (n != sizeof(len_net)) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] Failed to read length: %zd", n);
        return false;
    }
    
    uint32_t len = ntohl(len_net);
    if (len == 0 || len > 1024 * 1024) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] Invalid length: %u", len);
        return false;
    }
    
    out_json.resize(len);
    n = recv(fd, &out_json[0], len, MSG_WAITALL);
    if (n != static_cast<ssize_t>(len)) {
        mdclog_write(MDCLOG_ERR, "[AI-CONFIG] Failed to read body: %zd/%u", n, len);
        return false;
    }
    
    return true;
}

