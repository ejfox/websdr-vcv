#include "../plugin.hpp"
#include "../network/WebSDRClient.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

struct WebSDRModule : Module {
    enum ParamId {
        FREQ_PARAM,
        GAIN_PARAM,
        MODE_PARAM,
        PRESET_PARAM,
        PRESET_PARAM_LAST = PRESET_PARAM + 7,
        NUM_PARAMS
    };
    
    enum InputId {
        PRESET_GATE_INPUT,
        PRESET_GATE_INPUT_LAST = PRESET_GATE_INPUT + 7,
        NUM_INPUTS
    };
    
    enum OutputId {
        AUDIO_OUTPUT,
        NUM_OUTPUTS
    };
    
    enum LightId {
        CONNECTION_LIGHT,
        PRESET_LIGHT,
        PRESET_LIGHT_LAST = PRESET_LIGHT + 7,
        NUM_LIGHTS
    };
    
    WebSDRClient client;
    std::vector<float> audioBuffer;
    std::mutex bufferMutex;
    size_t readPos = 0;
    size_t writePos = 0;
    
    // Resampling from 12kHz to engine sample rate
    float lastSample = 0.0f;
    float resamplePhase = 0.0f;
    
    // Preset system
    static constexpr int NUM_PRESETS = 8;
    float presetFrequencies[NUM_PRESETS] = {};
    bool presetSaved[NUM_PRESETS] = {};
    dsp::SchmittTrigger presetTriggers[NUM_PRESETS];
    dsp::SchmittTrigger presetGateTriggers[NUM_PRESETS];
    float presetLightBrightness[NUM_PRESETS] = {};
    
    WebSDRModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configParam(FREQ_PARAM, 0.0f, 30000000.0f, 7055000.0f, "Frequency", " Hz");
        configParam(GAIN_PARAM, 0.0f, 2.0f, 1.0f, "Gain");
        configParam(MODE_PARAM, 0.0f, 4.0f, 0.0f, "Mode");
        
        // Configure preset buttons
        for (int i = 0; i < NUM_PRESETS; i++) {
            configButton(PRESET_PARAM + i, string::f("Preset %d", i + 1));
            configInput(PRESET_GATE_INPUT + i, string::f("Preset %d gate", i + 1));
            configLight(PRESET_LIGHT + i, string::f("Preset %d", i + 1));
            
            // Initialize with some default frequencies
            presetFrequencies[i] = 0.0f;
            presetSaved[i] = false;
        }
        
        configOutput(AUDIO_OUTPUT, "Audio");
        configLight(CONNECTION_LIGHT, "Connection");
        
        // Pre-allocate circular buffer (1 second at 48kHz)
        audioBuffer.resize(48000);
        
        // Set up audio callback
        client.setAudioCallback([this](const float* samples, size_t count) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            
            // Simple circular buffer write
            for (size_t i = 0; i < count; i++) {
                audioBuffer[writePos] = samples[i];
                writePos = (writePos + 1) % audioBuffer.size();
                
                // Prevent overflow - skip old samples if buffer is full
                if (writePos == readPos) {
                    readPos = (readPos + 1) % audioBuffer.size();
                }
            }
        });
        
        // Connect to real KiwiSDR server!
        // Known working servers:
        // - "kiwisdr.ve6slp.ca:8073" (Canada)
        // - "sdr.ve3sun.com:8073" (Canada backup)
        // - "kiwisdr.n3lga.com:8073" (USA)
        
        if (!client.connect("kiwisdr.ve6slp.ca:8073")) {
            // Try backup server
            client.connect("sdr.ve3sun.com:8073");
        }
    }
    
    ~WebSDRModule() {
        client.disconnect();
    }
    
    void process(const ProcessArgs& args) override {
        // Handle preset buttons and gates
        float currentFreq = params[FREQ_PARAM].getValue();
        
        for (int i = 0; i < NUM_PRESETS; i++) {
            // Check button press
            if (presetTriggers[i].process(params[PRESET_PARAM + i].getValue())) {
                handlePresetPress(i, currentFreq);
            }
            
            // Check gate input
            if (inputs[PRESET_GATE_INPUT + i].isConnected()) {
                if (presetGateTriggers[i].process(inputs[PRESET_GATE_INPUT + i].getVoltage())) {
                    if (presetSaved[i]) {
                        // Recall preset
                        params[FREQ_PARAM].setValue(presetFrequencies[i]);
                        flashPresetLight(i);
                    }
                }
            }
            
            // Update preset lights
            if (presetSaved[i]) {
                // Saved presets glow dimly
                presetLightBrightness[i] = std::max(0.2f, presetLightBrightness[i] - args.sampleTime * 2.0f);
            } else {
                // Unsaved presets fade out
                presetLightBrightness[i] = std::max(0.0f, presetLightBrightness[i] - args.sampleTime * 2.0f);
            }
            lights[PRESET_LIGHT + i].setBrightness(presetLightBrightness[i]);
        }
        
        // Update frequency if changed
        static float lastFreq = 0.0f;
        float freq = params[FREQ_PARAM].getValue();
        if (fabs(freq - lastFreq) > 100.0f) {  // Only update if changed significantly
            client.setFrequency(freq);
            lastFreq = freq;
        }
        
        // Update mode if changed
        static float lastMode = -1.0f;
        float mode = params[MODE_PARAM].getValue();
        if (mode != lastMode) {
            const char* modes[] = {"am", "fm", "usb", "lsb", "cw"};
            int modeIdx = std::min(4, std::max(0, (int)mode));
            client.setMode(modes[modeIdx]);
            lastMode = mode;
        }
        
        // Get audio sample with basic resampling
        // WebSDR typically sends 12kHz, we need to resample to engine rate
        float sample = getResampledAudio(args.sampleRate);
        
        // Apply gain and output
        outputs[AUDIO_OUTPUT].setVoltage(sample * params[GAIN_PARAM].getValue() * 5.0f);
        
        // Update connection light
        lights[CONNECTION_LIGHT].setBrightness(client.isConnected() ? 1.0f : 0.0f);
    }
    
    void handlePresetPress(int preset, float currentFreq) {
        if (!presetSaved[preset]) {
            // Save current frequency to this preset
            presetFrequencies[preset] = currentFreq;
            presetSaved[preset] = true;
            presetLightBrightness[preset] = 1.0f;  // Flash bright
        } else {
            // Recall preset
            params[FREQ_PARAM].setValue(presetFrequencies[preset]);
            flashPresetLight(preset);
        }
    }
    
    void flashPresetLight(int preset) {
        presetLightBrightness[preset] = 1.0f;
    }
    
    float getResampledAudio(float engineRate) {
        const float sourceRate = 12000.0f;  // KiwiSDR sample rate
        const float resampleRatio = sourceRate / engineRate;
        
        std::lock_guard<std::mutex> lock(bufferMutex);
        
        // Simple linear interpolation resampling
        resamplePhase += resampleRatio;
        
        if (resamplePhase >= 1.0f) {
            // Advance read position
            int advance = (int)resamplePhase;
            resamplePhase -= advance;
            
            for (int i = 0; i < advance; i++) {
                if (readPos != writePos) {
                    lastSample = audioBuffer[readPos];
                    readPos = (readPos + 1) % audioBuffer.size();
                }
            }
        }
        
        // Get next sample for interpolation
        float nextSample = lastSample;
        if (readPos != writePos) {
            size_t peekPos = readPos;
            nextSample = audioBuffer[peekPos];
        }
        
        // Linear interpolation
        return lastSample + (nextSample - lastSample) * resamplePhase;
    }
    
    void onReset() override {
        std::lock_guard<std::mutex> lock(bufferMutex);
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0.0f);
        readPos = 0;
        writePos = 0;
        lastSample = 0.0f;
        resamplePhase = 0.0f;
        
        // Clear presets
        for (int i = 0; i < NUM_PRESETS; i++) {
            presetFrequencies[i] = 0.0f;
            presetSaved[i] = false;
            presetLightBrightness[i] = 0.0f;
        }
    }
    
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        
        // Save presets
        json_t* presetsJ = json_array();
        for (int i = 0; i < NUM_PRESETS; i++) {
            json_t* presetJ = json_object();
            json_object_set_new(presetJ, "freq", json_real(presetFrequencies[i]));
            json_object_set_new(presetJ, "saved", json_boolean(presetSaved[i]));
            json_array_append_new(presetsJ, presetJ);
        }
        json_object_set_new(rootJ, "presets", presetsJ);
        
        return rootJ;
    }
    
    void dataFromJson(json_t* rootJ) override {
        // Load presets
        json_t* presetsJ = json_object_get(rootJ, "presets");
        if (presetsJ) {
            for (int i = 0; i < NUM_PRESETS; i++) {
                json_t* presetJ = json_array_get(presetsJ, i);
                if (presetJ) {
                    json_t* freqJ = json_object_get(presetJ, "freq");
                    if (freqJ) presetFrequencies[i] = json_real_value(freqJ);
                    
                    json_t* savedJ = json_object_get(presetJ, "saved");
                    if (savedJ) presetSaved[i] = json_boolean_value(savedJ);
                }
            }
        }
    }
};

// Frequency display widget
struct FrequencyDisplay : Widget {
    WebSDRModule* module;
    int presetIndex = -1;
    
    void draw(const DrawArgs& args) override {
        if (!module) return;
        
        // Background
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);
        
        // Current frequency display
        if (presetIndex < 0) {
            float freq = module->params[WebSDRModule::FREQ_PARAM].getValue();
            float freqMhz = freq / 1000000.0f;
            
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3) << freqMhz << " MHz";
            
            nvgFontSize(args.vg, 11);
            nvgFillColor(args.vg, nvgRGB(0, 255, 100));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, ss.str().c_str(), NULL);
        }
    }
};

// Module widget
struct WebSDRModuleWidget : ModuleWidget {
    WebSDRModuleWidget(WebSDRModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/WebSDRReceiver.svg")));
        
        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));
        
        // Frequency knob (centered)
        addParam(createParamCentered<RoundBigBlackKnob>(Vec(75, 100), module, WebSDRModule::FREQ_PARAM));
        
        // Gain knob (centered)
        addParam(createParamCentered<RoundBlackKnob>(Vec(75, 160), module, WebSDRModule::GAIN_PARAM));
        
        // Mode selector (snap knob)
        auto modeKnob = createParamCentered<RoundBlackSnapKnob>(Vec(75, 220), module, WebSDRModule::MODE_PARAM);
        modeKnob->minAngle = -0.5f * M_PI;
        modeKnob->maxAngle = 0.5f * M_PI;
        modeKnob->snap = true;
        addParam(modeKnob);
        
        // Preset buttons (2x4 grid)
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 2; col++) {
                int i = row * 2 + col;
                float x = 30 + col * 30;  // 30 and 60
                float y = 270 + row * 20;  // 270, 290, 310, 330
                
                // Preset button
                addParam(createParamCentered<TL1105>(Vec(x, y), module, WebSDRModule::PRESET_PARAM + i));
                
                // Preset light (next to button)
                addChild(createLightCentered<SmallLight<YellowLight>>(
                    Vec(x + 10, y), module, WebSDRModule::PRESET_LIGHT + i));
            }
        }
        
        // Gate inputs (4 on right side for presets 2,4,6,8)
        for (int i = 0; i < 4; i++) {
            int presetIdx = i * 2 + 1;  // 1, 3, 5, 7
            float y = 270 + i * 20;
            addInput(createInputCentered<PJ301MPort>(
                Vec(110, y), module, WebSDRModule::PRESET_GATE_INPUT + presetIdx));
        }
        
        // Audio output (centered)
        addOutput(createOutputCentered<PJ301MPort>(Vec(75, 360), module, WebSDRModule::AUDIO_OUTPUT));
        
        // Connection light (centered)
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(75, 40), module, WebSDRModule::CONNECTION_LIGHT));
        
        // Frequency display
        FrequencyDisplay* display = new FrequencyDisplay();
        display->module = module;
        display->box.pos = Vec(35, 55);
        display->box.size = Vec(80, 20);
        addChild(display);
    }
};

Model* modelWebSDRReceiver = createModel<WebSDRModule, WebSDRModuleWidget>("WebSDRReceiver");