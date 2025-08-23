// expander module for websdr - adds spectrum analyzer and extra controls
#include "../plugin.hpp"

struct WebSDRExpanderMessage {
    float spectrum[256] = {};
    float signalStrength = 0.0f;
    float frequency = 0.0f;
    bool connected = false;
};

struct WebSDRExpander : Module {
    enum ParamId {
        SCAN_PARAM,
        SCAN_SPEED_PARAM,
        NUM_PARAMS
    };
    
    enum InputId {
        SCAN_CV_INPUT,
        NUM_INPUTS
    };
    
    enum OutputId {
        SPECTRUM_OUTPUT,  // polyphonic spectrum data
        FREQ_OUTPUT,      // current frequency cv
        SCAN_OUTPUT,      // scanning cv output
        NUM_OUTPUTS
    };
    
    enum LightId {
        SCAN_LIGHT,
        NUM_LIGHTS
    };
    
    WebSDRExpanderMessage leftMessages[2][1];
    bool scanning = false;
    float scanPhase = 0.0f;
    
    WebSDRExpander() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configSwitch(SCAN_PARAM, 0.0f, 1.0f, 0.0f, "Frequency scan", {"Off", "On"});
        configParam(SCAN_SPEED_PARAM, 0.1f, 10.0f, 1.0f, "Scan speed", " Hz");
        
        configInput(SCAN_CV_INPUT, "Scan CV trigger");
        
        configOutput(SPECTRUM_OUTPUT, "Spectrum data")
            ->description = "Polyphonic output of spectrum analyzer (16 channels)";
        configOutput(FREQ_OUTPUT, "Frequency CV")
            ->description = "Current frequency as CV (1V = 1MHz)";
        configOutput(SCAN_OUTPUT, "Scan CV")
            ->description = "Ramp wave for frequency scanning";
        
        configLight(SCAN_LIGHT, "Scanning");
        
        leftExpander.producerMessage = leftMessages[0];
        leftExpander.consumerMessage = leftMessages[1];
    }
    
    void process(const ProcessArgs& args) override {
        // check if connected to main module
        if (leftExpander.module && leftExpander.module->model == modelWebSDRReceiver) {
            WebSDRExpanderMessage* msg = (WebSDRExpanderMessage*) leftExpander.consumerMessage;
            
            // output spectrum as polyphonic cable (16 channels)
            outputs[SPECTRUM_OUTPUT].setChannels(16);
            for (int i = 0; i < 16; i++) {
                float val = 0.0f;
                if (i < 256/16) {
                    // average 16 bins per channel
                    for (int j = 0; j < 16; j++) {
                        val += msg->spectrum[i * 16 + j];
                    }
                    val /= 16.0f;
                }
                outputs[SPECTRUM_OUTPUT].setVoltage(val * 10.0f, i);
            }
            
            // frequency cv output
            outputs[FREQ_OUTPUT].setVoltage(msg->frequency / 1000000.0f);  // 1v per mhz
            
            // scanning mode
            bool shouldScan = params[SCAN_PARAM].getValue() > 0.5f || 
                             inputs[SCAN_CV_INPUT].getVoltage() > 2.0f;
            
            if (shouldScan) {
                float speed = params[SCAN_SPEED_PARAM].getValue();
                scanPhase += speed * args.sampleTime;
                if (scanPhase > 1.0f) scanPhase -= 1.0f;
                
                outputs[SCAN_OUTPUT].setVoltage(scanPhase * 10.0f);
                lights[SCAN_LIGHT].setBrightness(1.0f);
            } else {
                outputs[SCAN_OUTPUT].setVoltage(0.0f);
                lights[SCAN_LIGHT].setBrightness(0.0f);
            }
            
        } else {
            // not connected
            outputs[SPECTRUM_OUTPUT].setChannels(0);
            outputs[FREQ_OUTPUT].setVoltage(0.0f);
            outputs[SCAN_OUTPUT].setVoltage(0.0f);
        }
    }
};

struct WebSDRExpanderWidget : ModuleWidget {
    WebSDRExpanderWidget(WebSDRExpander* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/WebSDRExpander.svg")));
        
        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        
        // scan controls
        addParam(createParamCentered<CKSS>(Vec(30, 100), module, WebSDRExpander::SCAN_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(30, 140), module, WebSDRExpander::SCAN_SPEED_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(30, 180), module, WebSDRExpander::SCAN_CV_INPUT));
        
        // outputs
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 240), module, WebSDRExpander::SPECTRUM_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 280), module, WebSDRExpander::FREQ_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 320), module, WebSDRExpander::SCAN_OUTPUT));
        
        // scan light
        addChild(createLightCentered<SmallLight<YellowLight>>(Vec(30, 120), module, WebSDRExpander::SCAN_LIGHT));
    }
};

Model* modelWebSDRExpander = createModel<WebSDRExpander, WebSDRExpanderWidget>("WebSDRExpander");