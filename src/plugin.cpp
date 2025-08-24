#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelWebSDRReceiver;
extern Model* modelSpectrumAnalyzer;
extern Model* modelStationScanner;

void init(Plugin* p) {
    pluginInstance = p;

    p->addModel(modelWebSDRReceiver);
    p->addModel(modelSpectrumAnalyzer);
    p->addModel(modelStationScanner);
}