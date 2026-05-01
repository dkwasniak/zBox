#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

// Replika logiki loadMappings() z main.cpp:739
// Działa na std::string zamiast SD — cross-platform.

static std::map<String, String> figurineMap;

static bool loadMappingsFromJson(const char* json)
{
    figurineMap.clear();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err != DeserializationError::Ok)
        return false;

    // Firmware: doc["figurines"].as<JsonObject>() → if (figurines) { populate }
    // Brak klucza "figurines" → pusta mapa, return true (tak jak firmware)
    JsonObject figurines = doc["figurines"].as<JsonObject>();
    if (figurines)
    {
        for (JsonPair kv : figurines)
        {
            String uid = kv.key().c_str();
            String file = kv.value()["file"].as<String>();
            figurineMap[uid] = file;
        }
    }
    return true;
}

void setUp() { figurineMap.clear(); }
void tearDown() {}

void test_valid_single_mapping()
{
    const char* json = R"({"figurines":{"04:A3:B2:C1":{"file":"song.mp3"}}})";
    TEST_ASSERT_TRUE(loadMappingsFromJson(json));
    TEST_ASSERT_EQUAL_INT(1, figurineMap.size());
}

void test_valid_multiple_mappings()
{
    const char* json = R"({"figurines":{
        "04:A3:B2:C1":{"file":"song1.mp3"},
        "04:11:22:33":{"file":"song2.mp3"}
    }})";
    TEST_ASSERT_TRUE(loadMappingsFromJson(json));
    TEST_ASSERT_EQUAL_INT(2, figurineMap.size());
}

void test_empty_figurines_object()
{
    const char* json = R"({"figurines":{}})";
    TEST_ASSERT_TRUE(loadMappingsFromJson(json));
    TEST_ASSERT_EQUAL_INT(0, figurineMap.size());
}

void test_invalid_json()
{
    TEST_ASSERT_FALSE(loadMappingsFromJson("{not valid json"));
}

void test_missing_figurines_key()
{
    // Firmware nie sprawdza obecności klucza — zwraca true z pustą mapą
    const char* json = R"({"other":{}})";
    TEST_ASSERT_TRUE(loadMappingsFromJson(json));
    TEST_ASSERT_EQUAL_INT(0, figurineMap.size());
}

void test_lookup_known_uid()
{
    const char* json = R"({"figurines":{"04:A3:B2:C1":{"file":"abc.mp3"}}})";
    loadMappingsFromJson(json);
    TEST_ASSERT_TRUE(figurineMap.find(String("04:A3:B2:C1")) != figurineMap.end());
}

void test_lookup_unknown_uid()
{
    const char* json = R"({"figurines":{"04:A3:B2:C1":{"file":"abc.mp3"}}})";
    loadMappingsFromJson(json);
    TEST_ASSERT_TRUE(figurineMap.find(String("FF:FF:FF:FF")) == figurineMap.end());
}

void test_filename_value_correct()
{
    const char* json = R"({"figurines":{"04:A3:B2:C1":{"file":"abc.mp3"}}})";
    loadMappingsFromJson(json);
    TEST_ASSERT_EQUAL_STRING("abc.mp3", figurineMap[String("04:A3:B2:C1")].c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_single_mapping);
    RUN_TEST(test_valid_multiple_mappings);
    RUN_TEST(test_empty_figurines_object);
    RUN_TEST(test_invalid_json);
    RUN_TEST(test_missing_figurines_key);
    RUN_TEST(test_lookup_known_uid);
    RUN_TEST(test_lookup_unknown_uid);
    RUN_TEST(test_filename_value_correct);
    return UNITY_END();
}
