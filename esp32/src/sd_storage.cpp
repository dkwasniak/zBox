#include "sd_storage.h"
#include <SPI.h>
#include <SD.h>
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"

bool initSD()
{
    LOGLN("Initializing SD card...");
    SPI.begin(18, 19, 23, SD_CS);
    // Usunięto delay(100) - SPI.begin() i SD.begin() obsługują timing wewnętrznie

    if (!SD.begin(SD_CS))
    {
        LOGLN("ERROR: SD mount failed!");
        return false;
    }

    LOG("SD Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

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
        LOGLN("No mappings.json on SD");
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
            LOG("  %s -> %s\n", uid.c_str(), file.c_str());
        }
    }

    LOG("Loaded %d figurines\n", figurineMap.size());
    return true;
}

void loadSystemSounds()
{
    systemSoundMap.clear();
    JsonDocument doc;
    if (!readJsonFromSd("/data/system_sounds.json", doc)) return;
    for (JsonPair kv : doc.as<JsonObject>())
        systemSoundMap[String(kv.key().c_str())] = kv.value().as<String>();
    LOG("[SYS] Loaded %d system sounds\n", (int)systemSoundMap.size());
}
