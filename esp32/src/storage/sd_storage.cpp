#include "sd_storage.h"
#include <SPI.h>
#include <SD.h>
#include <map>
#include <Arduino.h>
#include "zbox_config.h"
#include "logging.h"
#include "state.h"

namespace {
std::map<String, String> figurineMap;
std::map<String, String> systemSoundMap;
constexpr uint8_t SD_INIT_MAX_ATTEMPTS = 3;
constexpr uint16_t SD_INIT_SETTLE_MS = 60;
constexpr uint16_t SD_INIT_RETRY_DELAY_MS = 120;
}

bool initSD()
{
    LOGI("Initializing SD card...\n");
    for (uint8_t attempt = 1; attempt <= SD_INIT_MAX_ATTEMPTS; ++attempt) {
        SPI.end();
        delay(5);
        SPI.begin(18, 19, 23, SD_CS);
        delay(SD_INIT_SETTLE_MS);

        if (SD.begin(SD_CS)) {
            LOGI("[SD] Mount OK on attempt %u\n", (unsigned)attempt);
            LOGI("SD Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

            if (!SD.exists("/music"))
                SD.mkdir("/music");
            if (!SD.exists("/data"))
                SD.mkdir("/data");

            return true;
        }

        LOGW("[SD] Mount attempt %u/%u failed\n",
             (unsigned)attempt, (unsigned)SD_INIT_MAX_ATTEMPTS);
        if (attempt < SD_INIT_MAX_ATTEMPTS) {
            delay(SD_INIT_RETRY_DELAY_MS);
        }
    }

    LOGE("[SD] Mount failed\n");
    return false;
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

uint16_t mappingCount()
{
    return (uint16_t)figurineMap.size();
}

uint16_t systemSoundCount()
{
    return (uint16_t)systemSoundMap.size();
}

bool lookupMappingPath(const char *uid, char *out_path, size_t out_size)
{
    if (!uid || !*uid || !out_path || out_size == 0) return false;
    auto it = figurineMap.find(String(uid));
    if (it == figurineMap.end()) return false;
    String path = "/music/" + it->second;
    strlcpy(out_path, path.c_str(), out_size);
    return true;
}

bool lookupSystemSoundPath(const char *name, char *out_path, size_t out_size)
{
    if (!name || !*name || !out_path || out_size == 0) return false;
    auto it = systemSoundMap.find(String(name));
    if (it == systemSoundMap.end()) return false;
    strlcpy(out_path, it->second.c_str(), out_size);
    return true;
}
