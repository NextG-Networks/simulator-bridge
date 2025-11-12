#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

// Simple TCP server to receive configs from AI
// Runs in background thread, calls handler when config received
class AiConfigReceiver {
public:
    using ConfigHandler = std::function<bool(const std::string&)>;
    
    AiConfigReceiver(int port, ConfigHandler handler);
    ~AiConfigReceiver();
    
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

private:
    void run();
    bool handleConnection(int client_fd);
    bool recvFramed(int fd, std::string& out_json);
    
    int port_;
    int listen_fd_;
    ConfigHandler handler_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> server_thread_;
};

