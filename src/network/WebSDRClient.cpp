#include "WebSDRClient.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

WebSDRClient::WebSDRClient() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebSDRClient::~WebSDRClient() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool WebSDRClient::connect(const std::string& url) {
    serverUrl = url;
    
    // Parse URL - expects format like "kiwisdr.ve6slp.ca:8073"
    std::string host = url;
    int port = 8073;  // Default KiwiSDR port
    
    size_t colonPos = url.find(':');
    if (colonPos != std::string::npos) {
        host = url.substr(0, colonPos);
        port = std::stoi(url.substr(colonPos + 1));
    }
    
    std::cout << "[WebSDR] Connecting to " << host << ":" << port << std::endl;
    
    // DNS lookup
    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        std::cerr << "[WebSDR] Failed to resolve host" << std::endl;
        return false;
    }
    
    // Create socket
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        std::cerr << "[WebSDR] Failed to create socket" << std::endl;
        return false;
    }
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port);
    
    if (::connect(socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[WebSDR] Failed to connect" << std::endl;
        close(socketFd);
        socketFd = -1;
        return false;
    }
    
    // Send WebSocket upgrade request
    std::stringstream ws_request;
    ws_request << "GET /kiwi/" << port << "/SND HTTP/1.1\r\n"
               << "Host: " << host << ":" << port << "\r\n"
               << "Upgrade: websocket\r\n"
               << "Connection: Upgrade\r\n"
               << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
               << "Sec-WebSocket-Version: 13\r\n\r\n";
    
    send(socketFd, ws_request.str().c_str(), ws_request.str().length(), 0);
    
    // Check for 101 response
    char buffer[1024];
    int received = recv(socketFd, buffer, sizeof(buffer)-1, 0);
    
    if (received > 0) {
        buffer[received] = '\0';
        if (strstr(buffer, "101 Switching Protocols")) {
            std::cout << "[WebSDR] WebSocket connected!" << std::endl;
            connected = true;
            shouldStop = false;
            
            // Start receive thread
            receiveThread = std::thread(&WebSDRClient::receiveLoop, this);
            
            // Send initial commands
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET auth t=kiwi p=");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET AR OK in=12000 out=44100");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET squelch=0 max=0");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET genattn=0");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET mod=am low_cut=-4000 high_cut=4000 freq=7055.000");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sendWebSocketFrame("SET keepalive");
            sendWebSocketFrame("SET AUDIO_COMP=0");
            sendWebSocketFrame("SET AUDIO_START=1");
            
            return true;
        }
    }
    
    std::cerr << "[WebSDR] WebSocket upgrade failed" << std::endl;
    close(socketFd);
    socketFd = -1;
    return false;
}

void WebSDRClient::disconnect() {
    shouldStop = true;
    connected = false;
    
    if (socketFd >= 0) {
#ifdef _WIN32
        closesocket(socketFd);
#else
        close(socketFd);
#endif
        socketFd = -1;
    }
    
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

void WebSDRClient::setFrequency(float freq) {
    if (!connected) return;
    
    // KiwiSDR format: freq in kHz with 3 decimal places
    float freqKhz = freq / 1000.0f;
    std::stringstream ss;
    ss << "SET mod=am low_cut=-4000 high_cut=4000 freq=" 
       << std::fixed << std::setprecision(3) << freqKhz;
    
    if (sendWebSocketFrame(ss.str())) {
        std::cout << "[WebSDR] Frequency changed to " << freqKhz << " kHz" << std::endl;
    }
}

void WebSDRClient::setMode(const std::string& mode) {
    if (!connected) return;
    
    // Convert mode to KiwiSDR format
    std::string kiwi_mode = mode;
    if (mode == "usb") kiwi_mode = "usb";
    else if (mode == "lsb") kiwi_mode = "lsb";
    else if (mode == "fm") kiwi_mode = "nbfm";
    else if (mode == "cw") kiwi_mode = "cw";
    else kiwi_mode = "am";
    
    std::stringstream ss;
    ss << "SET mod=" << kiwi_mode << " low_cut=-4000 high_cut=4000";
    sendWebSocketFrame(ss.str());
}

void WebSDRClient::setBandwidth(float bw) {
    if (!connected) return;
    
    float half_bw = bw / 2.0f;
    std::stringstream ss;
    ss << "SET low_cut=" << -half_bw << " high_cut=" << half_bw;
    sendWebSocketFrame(ss.str());
}

void WebSDRClient::receiveLoop() {
    std::cout << "[WebSDR] Receive thread started" << std::endl;
    
    uint8_t buffer[8192];
    std::vector<float> audioSamples;
    
    while (!shouldStop && socketFd >= 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketFd, &readfds);
        
        struct timeval tv = {0, 100000};  // 100ms timeout
        
        if (select(socketFd + 1, &readfds, NULL, NULL, &tv) > 0) {
            int received = recv(socketFd, buffer, sizeof(buffer), 0);
            
            if (received > 0) {
                // Parse WebSocket frame
                if (received >= 2) {
                    uint8_t fin = (buffer[0] & 0x80) != 0;
                    uint8_t opcode = buffer[0] & 0x0F;
                    bool masked = (buffer[1] & 0x80) != 0;
                    uint64_t payloadLen = buffer[1] & 0x7F;
                    
                    size_t headerLen = 2;
                    if (payloadLen == 126 && received >= 4) {
                        payloadLen = (buffer[2] << 8) | buffer[3];
                        headerLen = 4;
                    } else if (payloadLen == 127 && received >= 10) {
                        // Large payload (not handling for now)
                        continue;
                    }
                    
                    if (!masked && headerLen < received) {  // Server->client shouldn't be masked
                        uint8_t* payload = buffer + headerLen;
                        size_t dataLen = std::min((size_t)payloadLen, (size_t)(received - headerLen));
                        
                        if (opcode == 2) {  // Binary frame = audio
                            processAudioPacket(payload, dataLen);
                        } else if (opcode == 1) {  // Text frame
                            std::string msg((char*)payload, dataLen);
                            // Log server messages for debugging
                            if (msg.find("MSG") != std::string::npos) {
                                // Skip verbose MSG frames
                            } else {
                                std::cout << "[WebSDR] Server message: " << msg.substr(0, 100) << std::endl;
                            }
                        } else if (opcode == 9) {  // Ping
                            // Send pong
                            buffer[0] = 0x8A;  // FIN + Pong
                            send(socketFd, buffer, received, 0);
                        } else if (opcode == 8) {  // Close
                            shouldStop = true;
                            break;
                        }
                    }
                }
            } else if (received == 0) {
                // Connection closed
                std::cout << "[WebSDR] Connection closed by server" << std::endl;
                break;
            } else {
                // Error
                std::cerr << "[WebSDR] Receive error" << std::endl;
                break;
            }
        }
    }
    
    connected = false;
    std::cout << "[WebSDR] Receive thread ended" << std::endl;
}

void WebSDRClient::processAudioPacket(const uint8_t* data, size_t len) {
    // KiwiSDR audio format: 
    // First check if it's a MSG frame (starts with "MSG ")
    if (len > 4 && memcmp(data, "MSG ", 4) == 0) {
        // Skip MSG frames for now
        return;
    }
    
    // Otherwise it's audio data
    // KiwiSDR sends 16-bit signed PCM audio
    std::vector<float> samples;
    samples.reserve(len / 2);
    
    for (size_t i = 0; i + 1 < len; i += 2) {
        // Little-endian 16-bit to float
        int16_t sample = (int16_t)(data[i] | (data[i+1] << 8));
        float normalized = sample / 32768.0f;
        samples.push_back(normalized);
    }
    
    // Send to callback
    if (!samples.empty()) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (audioCallback) {
            audioCallback(samples.data(), samples.size());
        }
    }
}

bool WebSDRClient::sendWebSocketFrame(const std::string& data) {
    if (socketFd < 0) return false;
    
    std::vector<uint8_t> frame;
    
    // FIN + text opcode
    frame.push_back(0x81);
    
    // Masked + length
    if (data.size() < 126) {
        frame.push_back(0x80 | data.size());
    } else {
        return false;  // Keep it simple for now
    }
    
    // Masking key
    uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    frame.insert(frame.end(), mask, mask + 4);
    
    // Masked payload
    for (size_t i = 0; i < data.size(); i++) {
        frame.push_back(data[i] ^ mask[i % 4]);
    }
    
    return send(socketFd, frame.data(), frame.size(), 0) > 0;
}

bool WebSDRClient::receiveWebSocketFrame(std::vector<uint8_t>& data) {
    // Not needed for now since receiveLoop handles everything
    return false;
}