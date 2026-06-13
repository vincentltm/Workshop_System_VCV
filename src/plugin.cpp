#include "plugin_local.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelWorkshopSystem);
    p->addModel(modelPedalboard);
}
