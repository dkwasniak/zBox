#include "sd_storage.h"
#include <SPI.h>
#include <SD.h>
#include "zbox_config.h"
#include "logging.h"
#include "state.h"

bool initSD()
{
    LOGI("Initializing SD card...\n");
    SPI.begin(18, 19, 23, SD_CS);
    // Removed delay(100) - SPI.begin() and SD.begin() handle timing internally

    if (!SD.begin(SD_CS))
    {
        LOGE("[SD] Mount failed\n");
        return false;
    }

    LOGI("SD Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    if (!SD.exists("/music"))
        SD.mkdir("/music");
    if (!SD.exists("/data"))
        SD.mkdir("/data");

    return true;
}

bool readJsonFromSd(const char *path, JsonDocument &doc)
{
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    bool ok = (deserializeJson(doc, f) == DeserializationError::Ok);
    f.close();
    return ok;
}

bool loadMappings()
{
    figurineMap.clear();

    JsonDocument doc;
    if (!readJsonFromSd("/data/mappings.json", doc))
    {
        LOGW("[SD] No mappings.json on SD\n");
        return false;
    }

    JsonObject figurines = doc["figurines"].as<JsonObject>();
    if (figurines)
    {
        for (JsonPair kv : figurines)
        {
            String uid = kv.key().c_str();
            String file = kv.value()["file"].as<String>();
            figurineMap[uid] = file;
            LOGI("  %s -> %s\n", uid.c_str(), file.c_str());
        }
    }

    LOGI("Loaded %d figurines\n", figurineMap.size());
    return true;
}

void loadSystemSounds()
{
    systemSoundMap.clear();
    JsonDocument doc;
    if (!readJsonFromSd("/data/system_sounds.json", doc)) return;
    for (JsonPair kv : doc.as<JsonObject>())
        systemSoundMap[String(kv.key().c_str())] = kv.value().as<String>();
    LOGI("[SYS] Loaded %d system sounds\n", (int)systemSoundMap.size());
}
