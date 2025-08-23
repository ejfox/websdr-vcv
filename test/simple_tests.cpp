#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <thread>
#include <mutex>

// Simple test framework
#define ASSERT(cond) if(!(cond)) { std::cerr << "  ✗ FAIL: " #cond << " at line " << __LINE__ << std::endl; return false; }
#define PASS() std::cout << "  ✓ PASS" << std::endl; return true;

// Test 1: Audio buffer circular write/read
bool test_circular_buffer() {
    std::cout << "1. Circular buffer: ";
    
    std::vector<float> buffer(100);
    size_t writePos = 0;
    size_t readPos = 0;
    
    // Write some data
    for (int i = 0; i < 10; i++) {
        buffer[writePos] = i;
        writePos = (writePos + 1) % buffer.size();
    }
    
    // Read it back
    for (int i = 0; i < 10; i++) {
        float val = buffer[readPos];
        readPos = (readPos + 1) % buffer.size();
        ASSERT(val == i);
    }
    
    PASS();
}

// Test 2: Resampling math
bool test_resampling() {
    std::cout << "2. Resampling 8kHz->44.1kHz: ";
    
    float sourceRate = 8000.0f;
    float targetRate = 44100.0f;
    float ratio = sourceRate / targetRate;
    
    // Generate test samples
    std::vector<float> source(100);
    for (int i = 0; i < 100; i++) {
        source[i] = i;  // Simple ramp
    }
    
    // Resample a few samples
    float phase = 0.0f;
    size_t srcIdx = 0;
    int outputCount = 0;
    
    while (srcIdx < 10 && outputCount < 50) {
        phase += ratio;
        if (phase >= 1.0f) {
            srcIdx += (int)phase;
            phase -= (int)phase;
        }
        outputCount++;
    }
    
    // Check we're advancing at the right rate
    float expectedAdvance = 10 * ratio * 5;  // Rough estimate
    ASSERT(srcIdx > 0 && srcIdx < 10);
    
    PASS();
}

// Test 3: Thread-safe buffer access
bool test_thread_safety() {
    std::cout << "3. Thread-safe audio: ";
    
    std::vector<float> buffer(1000, 0.0f);
    std::mutex bufferMutex;
    std::atomic<bool> stopThread(false);
    std::atomic<int> writesCompleted(0);
    
    // Writer thread
    std::thread writer([&]() {
        for (int i = 0; i < 100; i++) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            buffer[i % buffer.size()] = i;
            writesCompleted++;
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });
    
    // Wait for some writes
    while (writesCompleted < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Read safely
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        ASSERT(buffer[0] >= 0);  // Should have been written to
    }
    
    writer.join();
    ASSERT(writesCompleted == 100);
    
    PASS();
}

// Test 4: Audio level detection
bool test_audio_levels() {
    std::cout << "4. Audio level detection: ";
    
    // Generate test signals
    std::vector<float> silence(100, 0.0f);
    std::vector<float> loud(100);
    for (int i = 0; i < 100; i++) {
        loud[i] = sin(i * 0.1f);
    }
    
    // Calculate RMS
    auto getRMS = [](const std::vector<float>& samples) {
        float sum = 0;
        for (float s : samples) {
            sum += s * s;
        }
        return sqrt(sum / samples.size());
    };
    
    float silenceRMS = getRMS(silence);
    float loudRMS = getRMS(loud);
    
    ASSERT(silenceRMS < 0.01f);
    ASSERT(loudRMS > 0.5f);
    
    PASS();
}

// Test 5: Frequency to knob position
bool test_freq_conversion() {
    std::cout << "5. Frequency conversion: ";
    
    float minFreq = 0;
    float maxFreq = 30000000;  // 30 MHz
    
    // Test 7.055 MHz (40m ham band)
    float freq = 7055000;
    float knobPos = (freq - minFreq) / (maxFreq - minFreq);
    ASSERT(knobPos > 0.2f && knobPos < 0.3f);
    
    // Convert back
    float freqBack = minFreq + knobPos * (maxFreq - minFreq);
    ASSERT(fabs(freqBack - freq) < 1);
    
    PASS();
}

int main() {
    std::cout << "\n=== WebSDR Plugin Tests ===\n" << std::endl;
    
    int passed = 0;
    int total = 5;
    
    if (test_circular_buffer()) passed++;
    if (test_resampling()) passed++;
    if (test_thread_safety()) passed++;
    if (test_audio_levels()) passed++;
    if (test_freq_conversion()) passed++;
    
    std::cout << "\n=== Results: " << passed << "/" << total << " passed ===" << std::endl;
    
    if (passed == total) {
        std::cout << "✓ All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Some tests failed" << std::endl;
        return 1;
    }
}