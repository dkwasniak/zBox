#pragma once
#include <ArduinoJson.h>

bool initSD();
bool readJsonFromSd(const char *path, JsonDocument &doc);
bool loadMappings();
void loadSystemSounds();
