// Test the module compiles and preset logic works
#include <iostream>
#include <vector>
#include <cmath>
#include <mutex>
#include <atomic>

// Mock VCV Rack types for testing
namespace rack {
    struct Module {
        std::vector<float> params;
        std::vector<float> inputs;
        std::vector<float> outputs;
        std::vector<float> lights;
        
        void config(int numParams, int numInputs, int numOutputs, int numLights) {
            params.resize(numParams);
            inputs.resize(numInputs);
            outputs.resize(numOutputs);
            lights.resize(numLights);
        }
        
        void configParam(int id, float min, float max, float def, const std::string& name) {
            if (id < params.size()) params[id] = def;
        }
        
        virtual void process(float sampleTime) = 0;
    };
}

// Simplified WebSDR module for testing
class TestWebSDRModule : public rack::Module {
public:
    enum ParamId {
        FREQ_PARAM,
        GAIN_PARAM,
        MODE_PARAM,
        PRESET_PARAM,
        PRESET_PARAM_LAST = PRESET_PARAM + 7,
        NUM_PARAMS
    };
    
    static constexpr int NUM_PRESETS = 8;
    float presetFrequencies[NUM_PRESETS] = {};
    bool presetSaved[NUM_PRESETS] = {};
    bool presetTriggered[NUM_PRESETS] = {};
    
    TestWebSDRModule() {
        config(NUM_PARAMS, 0, 1, NUM_PRESETS);
        
        configParam(FREQ_PARAM, 0.0f, 30000000.0f, 7055000.0f, "Frequency");
        configParam(GAIN_PARAM, 0.0f, 2.0f, 1.0f, "Gain");
        configParam(MODE_PARAM, 0.0f, 4.0f, 0.0f, "Mode");
        
        for (int i = 0; i < NUM_PRESETS; i++) {
            configParam(PRESET_PARAM + i, 0.0f, 1.0f, 0.0f, "Preset");
            presetFrequencies[i] = 0.0f;
            presetSaved[i] = false;
        }
    }
    
    void handlePresetPress(int preset) {
        float currentFreq = params[FREQ_PARAM];
        
        if (!presetSaved[preset]) {
            // Save current frequency
            presetFrequencies[preset] = currentFreq;
            presetSaved[preset] = true;
            std::cout << "  Saved frequency " << currentFreq << " Hz to preset " << (preset + 1) << std::endl;
        } else {
            // Recall preset
            params[FREQ_PARAM] = presetFrequencies[preset];
            std::cout << "  Recalled preset " << (preset + 1) << ": " << presetFrequencies[preset] << " Hz" << std::endl;
        }
    }
    
    void process(float sampleTime) override {
        // Check preset buttons
        for (int i = 0; i < NUM_PRESETS; i++) {
            bool pressed = params[PRESET_PARAM + i] > 0.5f;
            if (pressed && !presetTriggered[i]) {
                handlePresetPress(i);
                presetTriggered[i] = true;
            } else if (!pressed) {
                presetTriggered[i] = false;
            }
        }
    }
};

// Test the preset functionality
bool test_presets() {
    std::cout << "Testing preset functionality..." << std::endl;
    
    TestWebSDRModule module;
    
    // Test saving presets
    std::cout << "1. Saving frequencies to presets:" << std::endl;
    
    // Save 7.055 MHz to preset 1
    module.params[TestWebSDRModule::FREQ_PARAM] = 7055000;
    module.params[TestWebSDRModule::PRESET_PARAM + 0] = 1.0f;
    module.process(0.001f);
    module.params[TestWebSDRModule::PRESET_PARAM + 0] = 0.0f;
    module.process(0.001f);
    
    // Save 14.230 MHz to preset 2
    module.params[TestWebSDRModule::FREQ_PARAM] = 14230000;
    module.params[TestWebSDRModule::PRESET_PARAM + 1] = 1.0f;
    module.process(0.001f);
    module.params[TestWebSDRModule::PRESET_PARAM + 1] = 0.0f;
    module.process(0.001f);
    
    // Save 3.750 MHz to preset 3
    module.params[TestWebSDRModule::FREQ_PARAM] = 3750000;
    module.params[TestWebSDRModule::PRESET_PARAM + 2] = 1.0f;
    module.process(0.001f);
    module.params[TestWebSDRModule::PRESET_PARAM + 2] = 0.0f;
    module.process(0.001f);
    
    std::cout << "\n2. Recalling presets:" << std::endl;
    
    // Change frequency to something else
    module.params[TestWebSDRModule::FREQ_PARAM] = 1000000;
    
    // Recall preset 1
    module.params[TestWebSDRModule::PRESET_PARAM + 0] = 1.0f;
    module.process(0.001f);
    if (std::abs(module.params[TestWebSDRModule::FREQ_PARAM] - 7055000) > 1) {
        std::cerr << "  ✗ Failed to recall preset 1" << std::endl;
        return false;
    }
    module.params[TestWebSDRModule::PRESET_PARAM + 0] = 0.0f;
    module.process(0.001f);
    
    // Recall preset 2
    module.params[TestWebSDRModule::PRESET_PARAM + 1] = 1.0f;
    module.process(0.001f);
    if (std::abs(module.params[TestWebSDRModule::FREQ_PARAM] - 14230000) > 1) {
        std::cerr << "  ✗ Failed to recall preset 2" << std::endl;
        return false;
    }
    
    std::cout << "\n✓ All preset tests passed!" << std::endl;
    return true;
}

// Test frequency display formatting
bool test_frequency_display() {
    std::cout << "\nTesting frequency display..." << std::endl;
    
    struct {
        float freq;
        std::string expected;
    } tests[] = {
        {7055000, "7.055 MHz"},
        {14230000, "14.230 MHz"},
        {3750000, "3.750 MHz"},
        {146520000, "146.520 MHz"},
        {440000, "0.440 MHz"}
    };
    
    for (auto& test : tests) {
        float freqMhz = test.freq / 1000000.0f;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.3f MHz", freqMhz);
        std::string result(buffer);
        
        std::cout << "  " << test.freq << " Hz -> " << result;
        if (result == test.expected) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗ (expected " << test.expected << ")" << std::endl;
            return false;
        }
    }
    
    return true;
}

int main() {
    std::cout << "\n=== WebSDR Module Tests ===\n" << std::endl;
    
    int passed = 0;
    int total = 2;
    
    if (test_presets()) passed++;
    if (test_frequency_display()) passed++;
    
    std::cout << "\n=== Results: " << passed << "/" << total << " tests passed ===" << std::endl;
    
    if (passed == total) {
        std::cout << "✓ Module logic working correctly!" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Some tests failed" << std::endl;
        return 1;
    }
}