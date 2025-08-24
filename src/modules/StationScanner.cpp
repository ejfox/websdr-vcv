// automatic station scanner module
#include "../plugin.hpp"
#include "stations.hpp"
#include <vector>
#include <algorithm>

struct StationScanner : Module {
    enum ParamId {
        SCAN_PARAM,
        SPEED_PARAM,
        MODE_PARAM,  // what to scan
        THRESHOLD_PARAM,
        NUM_PARAMS
    };
    
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        NUM_INPUTS
    };
    
    enum OutputId {
        FREQ_OUTPUT,
        GATE_OUTPUT,
        EOC_OUTPUT,  // end of cycle
        NUM_OUTPUTS
    };
    
    enum LightId {
        SCAN_LIGHT,
        NUM_LIGHTS
    };
    
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::PulseGenerator gatePulse;
    dsp::PulseGenerator eocPulse;
    
    int currentStation = 0;
    float scanTimer = 0.0f;
    bool scanning = false;
    
    enum ScanMode {
        ALL_STATIONS,
        TIME_SIGNALS,
        INTERNATIONAL,
        AMATEUR,
        MYSTERY,
        FAVORITES
    };
    
    std::vector<int> stationList;
    
    StationScanner() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configSwitch(SCAN_PARAM, 0.0f, 1.0f, 0.0f, "Scan", {"Off", "On"});
        configParam(SPEED_PARAM, 1.0f, 30.0f, 5.0f, "Dwell time", " seconds");
        configSwitch(MODE_PARAM, 0.0f, 5.0f, 0.0f, "Scan mode", {
            "All", "Time signals", "International", "Amateur", "Mystery", "Favorites"
        });
        configParam(THRESHOLD_PARAM, 0.0f, 10.0f, 0.0f, "Signal threshold", " V");
        
        configInput(CLOCK_INPUT, "Clock/trigger");
        configInput(RESET_INPUT, "Reset to first station");
        
        configOutput(FREQ_OUTPUT, "Frequency CV (1V/MHz)");
        configOutput(GATE_OUTPUT, "Station change trigger");
        configOutput(EOC_OUTPUT, "End of cycle trigger");
        
        configLight(SCAN_LIGHT, "Scanning");
        
        buildStationList();
    }
    
    void buildStationList() {
        stationList.clear();
        int mode = (int)params[MODE_PARAM].getValue();
        
        switch (mode) {
            case TIME_SIGNALS:
                for (int i = 0; i < NUM_STATIONS; i++) {
                    if (strstr(STATIONS[i].name, "wwv") || strstr(STATIONS[i].name, "chv")) {
                        stationList.push_back(i);
                    }
                }
                break;
                
            case INTERNATIONAL:
                for (int i = 0; i < NUM_STATIONS; i++) {
                    if (strstr(STATIONS[i].name, "bbc") || strstr(STATIONS[i].name, "voa") || 
                        strstr(STATIONS[i].name, "rhc") || strstr(STATIONS[i].name, "cri")) {
                        stationList.push_back(i);
                    }
                }
                break;
                
            case AMATEUR:
                for (int i = 0; i < NUM_STATIONS; i++) {
                    if (strstr(STATIONS[i].name, "ssb") || strstr(STATIONS[i].name, "ft8")) {
                        stationList.push_back(i);
                    }
                }
                break;
                
            case MYSTERY:
                for (int i = 0; i < NUM_STATIONS; i++) {
                    if (strstr(STATIONS[i].name, "uvb") || strstr(STATIONS[i].name, "hfgcs") || 
                        strstr(STATIONS[i].name, "pirate")) {
                        stationList.push_back(i);
                    }
                }
                break;
                
            case FAVORITES:
                for (int i = 0; i < sizeof(FAVORITES)/sizeof(int); i++) {
                    stationList.push_back(FAVORITES[i]);
                }
                break;
                
            default:  // ALL_STATIONS
                for (int i = 0; i < NUM_STATIONS; i++) {
                    stationList.push_back(i);
                }
                break;
        }
        
        if (stationList.empty()) {
            stationList.push_back(0);  // fallback
        }
    }
    
    void process(const ProcessArgs& args) override {
        // rebuild list if mode changed
        static int lastMode = -1;
        int mode = (int)params[MODE_PARAM].getValue();
        if (mode != lastMode) {
            buildStationList();
            lastMode = mode;
            currentStation = 0;
        }
        
        // reset
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
            currentStation = 0;
            scanTimer = 0.0f;
            gatePulse.trigger(0.01f);
        }
        
        // manual scan or auto scan
        bool shouldScan = params[SCAN_PARAM].getValue() > 0.5f;
        
        if (inputs[CLOCK_INPUT].isConnected()) {
            // clock mode - advance on trigger
            if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
                currentStation = (currentStation + 1) % stationList.size();
                gatePulse.trigger(0.01f);
                
                if (currentStation == 0) {
                    eocPulse.trigger(0.01f);
                }
            }
        } else if (shouldScan) {
            // auto scan mode
            float dwellTime = params[SPEED_PARAM].getValue();
            scanTimer += args.sampleTime;
            
            if (scanTimer >= dwellTime) {
                scanTimer = 0.0f;
                currentStation = (currentStation + 1) % stationList.size();
                gatePulse.trigger(0.01f);
                
                if (currentStation == 0) {
                    eocPulse.trigger(0.01f);
                }
            }
        }
        
        // output current station frequency
        if (currentStation < stationList.size()) {
            int stationIdx = stationList[currentStation];
            if (stationIdx < NUM_STATIONS) {
                float freq = STATIONS[stationIdx].freq;
                outputs[FREQ_OUTPUT].setVoltage(freq / 1000000.0f);  // 1V/MHz
            }
        }
        
        // gates
        outputs[GATE_OUTPUT].setVoltage(gatePulse.process(args.sampleTime) ? 10.0f : 0.0f);
        outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.0f : 0.0f);
        
        // light
        lights[SCAN_LIGHT].setBrightness(shouldScan ? 1.0f : 0.0f);
    }
    
    void onReset() override {
        currentStation = 0;
        scanTimer = 0.0f;
    }
    
    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "currentStation", json_integer(currentStation));
        return rootJ;
    }
    
    void dataFromJson(json_t* rootJ) override {
        json_t* currentStationJ = json_object_get(rootJ, "currentStation");
        if (currentStationJ)
            currentStation = json_integer_value(currentStationJ);
    }
};

struct StationDisplay : Widget {
    StationScanner* module;
    
    void draw(const DrawArgs& args) override {
        if (!module) return;
        
        // background
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(10, 10, 10));
        nvgFill(args.vg);
        
        // current station
        if (module->currentStation < module->stationList.size()) {
            int idx = module->stationList[module->currentStation];
            if (idx < NUM_STATIONS) {
                const Station* station = &STATIONS[idx];
                
                nvgFontSize(args.vg, 10);
                nvgFillColor(args.vg, nvgRGB(0, 255, 100));
                nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                
                // station name
                nvgText(args.vg, box.size.x/2, box.size.y/2 - 8, station->name, NULL);
                
                // frequency
                char freqStr[32];
                snprintf(freqStr, sizeof(freqStr), "%.3f MHz", station->freq / 1000000.0f);
                nvgFontSize(args.vg, 9);
                nvgFillColor(args.vg, nvgRGB(0, 200, 80));
                nvgText(args.vg, box.size.x/2, box.size.y/2 + 8, freqStr, NULL);
                
                // station number
                char numStr[32];
                snprintf(numStr, sizeof(numStr), "%d/%d", module->currentStation + 1, (int)module->stationList.size());
                nvgFontSize(args.vg, 8);
                nvgFillColor(args.vg, nvgRGB(100, 100, 100));
                nvgText(args.vg, box.size.x/2, box.size.y - 5, numStr, NULL);
            }
        }
    }
};

struct StationScannerWidget : ModuleWidget {
    StationScannerWidget(StationScanner* module) {
        setModule(module);
        
        // simple 6hp panel
        setPanel(createPanel(asset::plugin(pluginInstance, "res/StationScanner.svg")));
        
        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        
        // display
        StationDisplay* display = new StationDisplay();
        display->module = module;
        display->box.pos = Vec(10, 30);
        display->box.size = Vec(70, 60);
        addChild(display);
        
        // scan switch
        addParam(createParamCentered<CKSS>(Vec(45, 110), module, StationScanner::SCAN_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(45, 130), module, StationScanner::SCAN_LIGHT));
        
        // speed knob
        addParam(createParamCentered<RoundBlackKnob>(Vec(25, 160), module, StationScanner::SPEED_PARAM));
        
        // mode selector
        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(65, 160), module, StationScanner::MODE_PARAM));
        
        // inputs
        addInput(createInputCentered<PJ301MPort>(Vec(25, 220), module, StationScanner::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(65, 220), module, StationScanner::RESET_INPUT));
        
        // outputs
        addOutput(createOutputCentered<PJ301MPort>(Vec(25, 280), module, StationScanner::FREQ_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(65, 280), module, StationScanner::GATE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 320), module, StationScanner::EOC_OUTPUT));
    }
};

Model* modelStationScanner = createModel<StationScanner, StationScannerWidget>("StationScanner");