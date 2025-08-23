#include "../plugin.hpp"
#include <cmath>

struct SpectrumAnalyzerModule : Module {
    enum ParamId {
        NUM_PARAMS
    };
    
    enum InputId {
        AUDIO_INPUT,
        NUM_INPUTS
    };
    
    enum OutputId {
        NUM_OUTPUTS
    };
    
    enum LightId {
        NUM_LIGHTS
    };
    
    static constexpr int FFT_SIZE = 128;
    float spectrum[FFT_SIZE] = {};
    
    SpectrumAnalyzerModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configInput(AUDIO_INPUT, "Audio");
    }
    
    void process(const ProcessArgs& args) override {
        if (!inputs[AUDIO_INPUT].isConnected()) return;
        
        float sample = inputs[AUDIO_INPUT].getVoltage() / 5.0f;
        
        // Simple peak detection for visualization (not real FFT)
        static int idx = 0;
        spectrum[idx] = fabsf(sample);
        idx = (idx + 1) % FFT_SIZE;
    }
};

struct SpectrumDisplay : Widget {
    SpectrumAnalyzerModule* module;
    
    void draw(const DrawArgs& args) override {
        if (!module) return;
        
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(10, 10, 10));
        nvgFill(args.vg);
        
        // Draw spectrum bars
        float barWidth = box.size.x / SpectrumAnalyzerModule::FFT_SIZE;
        for (int i = 0; i < SpectrumAnalyzerModule::FFT_SIZE; i++) {
            float height = module->spectrum[i] * box.size.y;
            
            nvgBeginPath(args.vg);
            nvgRect(args.vg, i * barWidth, box.size.y - height, barWidth - 1, height);
            nvgFillColor(args.vg, nvgRGB(0, 255, 100));
            nvgFill(args.vg);
        }
    }
};

struct SpectrumAnalyzerWidget : ModuleWidget {
    SpectrumAnalyzerWidget(SpectrumAnalyzerModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SpectrumAnalyzer.svg")));
        
        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));
        
        // Spectrum display
        SpectrumDisplay* display = new SpectrumDisplay();
        display->module = module;
        display->box.pos = Vec(10, 40);
        display->box.size = Vec(160, 100);
        addChild(display);
        
        // Audio input
        addInput(createInputCentered<PJ301MPort>(Vec(90, 320), module, SpectrumAnalyzerModule::AUDIO_INPUT));
    }
};

Model* modelSpectrumAnalyzer = createModel<SpectrumAnalyzerModule, SpectrumAnalyzerWidget>("SpectrumAnalyzer");