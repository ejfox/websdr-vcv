#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class WebSDRClient {
public:
    WebSDRClient();
    ~WebSDRClient();
    
    bool connect(const std::string& url);
    void disconnect();
    bool isConnected() const { return connected.load(); }
    
    void setFrequency(float freq);
    void setMode(const std::string& mode);
    void setBandwidth(float bw);
    
    // Callback for received audio data
    void setAudioCallback(std::function<void(const float*, size_t)> callback) {
        audioCallback = callback;
    }
    
private:
    std::atomic<bool> connected{false};
    std::atomic<bool> shouldStop{false};
    std::thread receiveThread;
    
    std::string serverUrl;
    int socketFd = -1;
    
    std::mutex callbackMutex;
    std::function<void(const float*, size_t)> audioCallback;
    
    // WebSDR protocol handling
    void receiveLoop();
    bool sendCommand(const std::string& cmd);
    void processAudioPacket(const uint8_t* data, size_t len);
    
    // Simple WebSocket frame handling
    bool sendWebSocketFrame(const std::string& data);
    bool receiveWebSocketFrame(std::vector<uint8_t>& data);
};