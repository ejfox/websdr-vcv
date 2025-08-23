#include "../src/network/WebSDRClient.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cmath>

std::atomic<size_t> totalSamplesReceived{0};
std::atomic<size_t> audioPackets{0};
std::atomic<bool> receivingAudio{false};

void audioCallback(const float* samples, size_t count) {
    totalSamplesReceived += count;
    audioPackets++;
    receivingAudio = true;
    
    // Calculate RMS for level display
    float sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += samples[i] * samples[i];
    }
    float rms = sqrt(sum / count);
    
    // Show audio level meter
    int meterLevel = (int)(rms * 40);
    std::cout << "[AUDIO] Packet #" << audioPackets << " (" << count << " samples) [";
    for (int i = 0; i < 40; i++) {
        std::cout << (i < meterLevel ? "█" : "░");
    }
    std::cout << "] RMS: " << rms << std::endl;
}

int main() {
    std::cout << "\n=== WebSDR Plugin Integration Test ===\n" << std::endl;
    std::cout << "Testing full audio pipeline: KiwiSDR → WebSDRClient → Audio Callback\n" << std::endl;
    
    WebSDRClient client;
    
    // Set up audio callback
    client.setAudioCallback(audioCallback);
    
    // Try to connect
    std::cout << "Connecting to KiwiSDR server..." << std::endl;
    
    if (client.connect("kiwisdr.ve6slp.ca:8073")) {
        std::cout << "✓ Connected successfully!" << std::endl;
        
        // Let it run for 10 seconds
        std::cout << "\nReceiving audio for 10 seconds..." << std::endl;
        
        auto start = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
            if (receivingAudio) {
                // Change frequency after 5 seconds
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                    static bool changed = false;
                    if (!changed) {
                        std::cout << "\n>>> Changing frequency to 14.230 MHz..." << std::endl;
                        client.setFrequency(14230000);  // 20m band
                        changed = true;
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "\nDisconnecting..." << std::endl;
        client.disconnect();
        
        // Final stats
        std::cout << "\n=== Final Statistics ===" << std::endl;
        std::cout << "Total audio packets: " << audioPackets << std::endl;
        std::cout << "Total samples received: " << totalSamplesReceived << std::endl;
        std::cout << "Average packet size: " << (audioPackets > 0 ? totalSamplesReceived / audioPackets : 0) << " samples" << std::endl;
        std::cout << "Approx. seconds of audio: " << totalSamplesReceived / 12000.0f << std::endl;
        
        if (audioPackets > 0) {
            std::cout << "\n✓✓✓ SUCCESS! Real radio audio received and processed!" << std::endl;
            return 0;
        } else {
            std::cout << "\n✗ No audio received" << std::endl;
            return 1;
        }
    } else {
        std::cout << "✗ Failed to connect" << std::endl;
        
        // Try backup server
        std::cout << "\nTrying backup server..." << std::endl;
        if (client.connect("sdr.ve3sun.com:8073")) {
            std::cout << "✓ Connected to backup!" << std::endl;
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
            client.disconnect();
        }
        
        return 1;
    }
}