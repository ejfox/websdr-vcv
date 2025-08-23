#include "plugin.hpp"

Plugin* pluginInstance;

extern Model* modelWebSDRReceiver;
extern Model* modelSpectrumAnalyzer;

void init(Plugin* p) {
    pluginInstance = p;

    p->addModel(modelWebSDRReceiver);
    p->addModel(modelSpectrumAnalyzer);
}