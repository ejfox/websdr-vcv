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
        FINE_PARAM,
        GAIN_PARAM,
        MODE_PARAM,
        PRESET_PARAM,
        PRESET_PARAM_LAST = PRESET_PARAM + 7,
        NUM_PARAMS
    };
    
    enum InputId {
        FREQ_CV_INPUT,
        FINE_CV_INPUT,
        GAIN_CV_INPUT,
        PRESET_GATE_INPUT,
        PRESET_GATE_INPUT_LAST = PRESET_GATE_INPUT + 7,
        NUM_INPUTS
    };
    
    enum OutputId {
        AUDIO_OUTPUT,
        SIGNAL_STRENGTH_OUTPUT,
        CARRIER_OUTPUT,  // outputs carrier frequency detection
        NUM_OUTPUTS
    };
    
    enum LightId {
        CONNECTION_LIGHT,
        SIGNAL_LIGHT_R,
        SIGNAL_LIGHT_G,
        SIGNAL_LIGHT_B,
        PRESET_LIGHT,
        PRESET_LIGHT_LAST = PRESET_LIGHT + 7,
        NUM_LIGHTS
    };
    
    WebSDRClient client;
    std::vector<float> audioBuffer;
    std::mutex bufferMutex;
    size_t readPos = 0;
    size_t writePos = 0;
    
    // resampling
    float lastSample = 0.0f;
    float resamplePhase = 0.0f;
    
    // presets
    static constexpr int NUM_PRESETS = 8;
    float presetFrequencies[NUM_PRESETS] = {};
    bool presetSaved[NUM_PRESETS] = {};
    dsp::SchmittTrigger presetTriggers[NUM_PRESETS];
    dsp::SchmittTrigger presetGateTriggers[NUM_PRESETS];
    float presetLightBrightness[NUM_PRESETS] = {};
    
    // signal analysis
    dsp::RCFilter signalDetector;
    float signalStrength = 0.0f;
    float carrierFreq = 0.0f;
    
    // polyphony support
    int polyChannels = 1;
    
    // context menu options
    std::string serverUrl = "kiwisdr.ve6slp.ca:8073";
    bool autoReconnect = true;
    float bufferSize = 1.0f;  // seconds
    
    WebSDRModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        // main controls with better tooltips
        configParam(FREQ_PARAM, 0.0f, 30000000.0f, 7055000.0f, "Frequency", " Hz")
            ->description = "Radio frequency to tune. Try 7.200 MHz for amateur radio.";
        configParam(FINE_PARAM, -1000.0f, 1000.0f, 0.0f, "Fine tune", " Hz")
            ->description = "Fine frequency adjustment for precise tuning";
        configParam(GAIN_PARAM, 0.0f, 2.0f, 1.0f, "Gain")
            ->description = "Output gain control";
        configParam(MODE_PARAM, 0.0f, 4.0f, 0.0f, "Mode")
            ->description = "Demodulation mode: AM/FM/USB/LSB/CW";
        
        // preset buttons
        for (int i = 0; i < NUM_PRESETS; i++) {
            configButton(PRESET_PARAM + i, string::f("Preset %d", i + 1));
            configInput(PRESET_GATE_INPUT + i, string::f("Preset %d gate trigger", i + 1));
            configLight(PRESET_LIGHT + i, string::f("Preset %d saved", i + 1));
        }
        
        // cv inputs
        configInput(FREQ_CV_INPUT, "Frequency CV")
            ->description = "1V/Oct or 0-10V frequency control";
        configInput(FINE_CV_INPUT, "Fine tune CV")
            ->description = "Fine frequency adjustment CV";
        configInput(GAIN_CV_INPUT, "Gain CV")
            ->description = "VCA control input";
        
        // outputs
        configOutput(AUDIO_OUTPUT, "Audio")
            ->description = "Demodulated radio audio output";
        configOutput(SIGNAL_STRENGTH_OUTPUT, "Signal strength")
            ->description = "0-10V signal strength indicator";
        configOutput(CARRIER_OUTPUT, "Carrier detect")
            ->description = "Gate high when carrier detected";
        
        // lights
        configLight(CONNECTION_LIGHT, "Connection status");
        configLight<RedGreenBlueLight>(SIGNAL_LIGHT_R, "Signal quality");
        
        // bypass routing
        configBypass(AUDIO_OUTPUT, AUDIO_OUTPUT);
        
        // pre-allocate buffer
        audioBuffer.resize(48000 * bufferSize);
        
        // setup audio callback
        client.setAudioCallback([this](const float* samples, size_t count) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            
            for (size_t i = 0; i < count; i++) {
                audioBuffer[writePos] = samples[i];
                writePos = (writePos + 1) % audioBuffer.size();
                
                if (writePos == readPos) {
                    readPos = (readPos + 1) % audioBuffer.size();
                }
                
                // update signal strength
                signalDetector.process(fabsf(samples[i]));
                signalStrength = signalDetector.lowpass();
            }
        });
        
        // auto-connect
        connectToServer();
    }
    
    ~WebSDRModule() {
        client.disconnect();
    }
    
    void connectToServer() {
        if (!client.connect(serverUrl)) {
            // try backup servers
            if (!client.connect("sdr.ve3sun.com:8073")) {
                client.connect("kiwisdr.n3lga.com:8073");
            }
        }
    }
    
    void process(const ProcessArgs& args) override {
        // handle cv inputs
        float freqCV = 0.0f;
        if (inputs[FREQ_CV_INPUT].isConnected()) {
            // 1v/oct mode or 0-10v mode
            freqCV = inputs[FREQ_CV_INPUT].getVoltage() * 1000000.0f;  // 1MHz per volt
        }
        
        float fineCV = 0.0f;
        if (inputs[FINE_CV_INPUT].isConnected()) {
            fineCV = inputs[FINE_CV_INPUT].getVoltage() * 100.0f;  // 100Hz per volt
        }
        
        float gainCV = 1.0f;
        if (inputs[GAIN_CV_INPUT].isConnected()) {
            gainCV = clamp(inputs[GAIN_CV_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        }
        
        // calculate final frequency
        float totalFreq = params[FREQ_PARAM].getValue() + freqCV + 
                         params[FINE_PARAM].getValue() + fineCV;
        totalFreq = clamp(totalFreq, 0.0f, 30000000.0f);
        
        // update frequency if changed
        static float lastFreq = 0.0f;
        if (fabs(totalFreq - lastFreq) > 10.0f) {
            client.setFrequency(totalFreq);
            lastFreq = totalFreq;
        }
        
        // handle presets
        float currentFreq = params[FREQ_PARAM].getValue();
        for (int i = 0; i < NUM_PRESETS; i++) {
            if (presetTriggers[i].process(params[PRESET_PARAM + i].getValue())) {
                handlePresetPress(i, currentFreq);
            }
            
            if (inputs[PRESET_GATE_INPUT + i].isConnected()) {
                if (presetGateTriggers[i].process(inputs[PRESET_GATE_INPUT + i].getVoltage())) {
                    if (presetSaved[i]) {
                        params[FREQ_PARAM].setValue(presetFrequencies[i]);
                        flashPresetLight(i);
                    }
                }
            }
            
            // update preset lights
            if (presetSaved[i]) {
                presetLightBrightness[i] = std::max(0.2f, presetLightBrightness[i] - args.sampleTime * 2.0f);
            } else {
                presetLightBrightness[i] = std::max(0.0f, presetLightBrightness[i] - args.sampleTime * 2.0f);
            }
            lights[PRESET_LIGHT + i].setBrightness(presetLightBrightness[i]);
        }
        
        // get audio
        float sample = getResampledAudio(args.sampleRate);
        
        // apply gain with cv
        float totalGain = params[GAIN_PARAM].getValue() * gainCV;
        outputs[AUDIO_OUTPUT].setVoltage(sample * totalGain * 5.0f);
        
        // signal strength output (0-10v)
        outputs[SIGNAL_STRENGTH_OUTPUT].setVoltage(signalStrength * 10.0f);
        
        // carrier detect (gate)
        outputs[CARRIER_OUTPUT].setVoltage(signalStrength > 0.1f ? 10.0f : 0.0f);
        
        // update lights
        lights[CONNECTION_LIGHT].setBrightness(client.isConnected() ? 1.0f : 0.0f);
        
        // rgb signal light (green=good, yellow=ok, red=poor)
        lights[SIGNAL_LIGHT_R].setBrightness(signalStrength < 0.3f ? 1.0f : 0.0f);
        lights[SIGNAL_LIGHT_G].setBrightness(signalStrength > 0.1f ? 1.0f : 0.0f);
        lights[SIGNAL_LIGHT_B].setBrightness(0.0f);
        
        // auto-reconnect if needed
        if (autoReconnect && !client.isConnected()) {
            static float reconnectTimer = 0.0f;
            reconnectTimer += args.sampleTime;
            if (reconnectTimer > 5.0f) {  // try every 5 seconds
                reconnectTimer = 0.0f;
                connectToServer();
            }
        }
    }
    
    void handlePresetPress(int preset, float currentFreq) {
        if (!presetSaved[preset]) {
            presetFrequencies[preset] = currentFreq;
            presetSaved[preset] = true;
            presetLightBrightness[preset] = 1.0f;
        } else {
            params[FREQ_PARAM].setValue(presetFrequencies[preset]);
            flashPresetLight(preset);
        }
    }
    
    void flashPresetLight(int preset) {
        presetLightBrightness[preset] = 1.0f;
    }
    
    float getResampledAudio(float engineRate) {
        const float sourceRate = 12000.0f;
        const float resampleRatio = sourceRate / engineRate;
        
        std::lock_guard<std::mutex> lock(bufferMutex);
        
        resamplePhase += resampleRatio;
        
        if (resamplePhase >= 1.0f) {
            int advance = (int)resamplePhase;
            resamplePhase -= advance;
            
            for (int i = 0; i < advance; i++) {
                if (readPos != writePos) {
                    lastSample = audioBuffer[readPos];
                    readPos = (readPos + 1) % audioBuffer.size();
                }
            }
        }
        
        float nextSample = lastSample;
        if (readPos != writePos) {
            size_t peekPos = readPos;
            nextSample = audioBuffer[peekPos];
        }
        
        return lastSample + (nextSample - lastSample) * resamplePhase;
    }
    
    // context menu
    void appendContextMenu(Menu* menu) override {
        menu->addChild(new MenuSeparator);
        
        menu->addChild(createMenuLabel("WebSDR Settings"));
        
        // server selection
        struct ServerItem : MenuItem {
            WebSDRModule* module;
            std::string url;
            void onAction(const event::Action& e) override {
                module->serverUrl = url;
                module->client.disconnect();
                module->client.connect(url);
            }
        };
        
        menu->addChild(createSubmenuItem("Server", serverUrl, [=](Menu* menu) {
            for (auto& server : {"kiwisdr.ve6slp.ca:8073", "sdr.ve3sun.com:8073", "kiwisdr.n3lga.com:8073"}) {
                ServerItem* item = new ServerItem;
                item->text = server;
                item->module = this;
                item->url = server;
                item->rightText = (serverUrl == server) ? "✓" : "";
                menu->addChild(item);
            }
        }));
        
        // auto-reconnect toggle
        struct AutoReconnectItem : MenuItem {
            WebSDRModule* module;
            void onAction(const event::Action& e) override {
                module->autoReconnect = !module->autoReconnect;
            }
        };
        
        AutoReconnectItem* autoItem = new AutoReconnectItem;
        autoItem->text = "Auto-reconnect";
        autoItem->module = this;
        autoItem->rightText = autoReconnect ? "✓" : "";
        menu->addChild(autoItem);
        
        // buffer size
        struct BufferItem : MenuItem {
            WebSDRModule* module;
            float size;
            void onAction(const event::Action& e) override {
                module->bufferSize = size;
                module->audioBuffer.resize(48000 * size);
            }
        };
        
        menu->addChild(createSubmenuItem("Buffer size", string::f("%.1fs", bufferSize), [=](Menu* menu) {
            for (float size : {0.5f, 1.0f, 2.0f, 5.0f}) {
                BufferItem* item = new BufferItem;
                item->text = string::f("%.1f seconds", size);
                item->module = this;
                item->size = size;
                item->rightText = (bufferSize == size) ? "✓" : "";
                menu->addChild(item);
            }
        }));
    }
    
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        
        // save presets
        json_t* presetsJ = json_array();
        for (int i = 0; i < NUM_PRESETS; i++) {
            json_t* presetJ = json_object();
            json_object_set_new(presetJ, "freq", json_real(presetFrequencies[i]));
            json_object_set_new(presetJ, "saved", json_boolean(presetSaved[i]));
            json_array_append_new(presetsJ, presetJ);
        }
        json_object_set_new(rootJ, "presets", presetsJ);
        
        // save settings
        json_object_set_new(rootJ, "serverUrl", json_string(serverUrl.c_str()));
        json_object_set_new(rootJ, "autoReconnect", json_boolean(autoReconnect));
        json_object_set_new(rootJ, "bufferSize", json_real(bufferSize));
        
        return rootJ;
    }
    
    void dataFromJson(json_t* rootJ) override {
        // load presets
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
        
        // load settings
        json_t* serverUrlJ = json_object_get(rootJ, "serverUrl");
        if (serverUrlJ) serverUrl = json_string_value(serverUrlJ);
        
        json_t* autoReconnectJ = json_object_get(rootJ, "autoReconnect");
        if (autoReconnectJ) autoReconnect = json_boolean_value(autoReconnectJ);
        
        json_t* bufferSizeJ = json_object_get(rootJ, "bufferSize");
        if (bufferSizeJ) bufferSize = json_real_value(bufferSizeJ);
    }
};