#pragma once
#include <ArduinoJson.h>

bool initSD();
bool readJsonFromSd(const char *path, JsonDocument &doc);
bool loadMappings();
void loadSystemSounds();
uint16_t mappingCount();
uint16_t systemSoundCount();
bool lookupMappingPath(const char *uid, char *out_path, size_t out_size);
bool lookupSystemSoundPath(const char *name, char *out_path, size_t out_size);
