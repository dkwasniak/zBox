/*
 * MusicBox - Muzyczne Pudełko dla Dzieci
 *
 * ESP32 + PN532 (NFC SPI) → sterowanie Yamaha YAS-209 przez DLNA
 * ESP32 czyta tagi NFC i odtwarza muzykę bezpośrednio na soundbarze przez UPnP/DLNA.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebSerial.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// =============================================================================
// PINY
// =============================================================================

// PN532 NFC (SPI)
#define PN532_SCK  18
#define PN532_MISO 19
#define PN532_MOSI 23
#define PN532_SS   5

// LED
#define LED_PIN 2

// =============================================================================
// KONFIGURACJA
// =============================================================================

// WiFi
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// MusicBox server (źródło streamów)
#ifndef SERVER_HOST
#define SERVER_HOST "musicbox-server.local"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT 8000
#endif

// DLNA - Yamaha YAS-209 (wykrywana automatycznie przez SSDP)
#define DLNA_TRANSPORT_URL "/upnp/control/rendertransport1"
#define DLNA_TARGET_UUID "uuid:FFB8F002-F478-76B8-9813-5248FFB8F002"
#define SSDP_DISCOVER_TIMEOUT 5000  // ms
#define SSDP_RETRY_INTERVAL 10000   // ms między próbami rediscovery

#define NFC_READ_INTERVAL  300
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD   3     // 3 * 300ms = ~1s bez tagu przed pauzą

// =============================================================================
// OBIEKTY
// =============================================================================

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
AsyncWebServer webServer(80);

// Log do Serial + WebSerial jednocześnie
void log(const String& msg) {
    Serial.println(msg);
    WebSerial.println(msg);
}

void log(const char* msg) {
    Serial.println(msg);
    WebSerial.println(msg);
}

void logPrint(const String& msg) {
    Serial.print(msg);
    WebSerial.print(msg);
}

void logPrint(const char* msg) {
    Serial.print(msg);
    WebSerial.print(msg);
}

// =============================================================================
// STAN
// =============================================================================

String currentNfcUid = "";
String lastNfcUid = "";
bool isPlaying = false;
bool nfcReady = false;

// DLNA discovery
String dlnaHost = "";
int dlnaPort = 0;
bool dlnaFound = false;
unsigned long lastDiscoveryAttempt = 0;

unsigned long lastNfcRead = 0;
int noTagCount = 0;
int nfcErrorCount = 0;

// =============================================================================
// HELPERS
// =============================================================================

String uidToString(uint8_t* uid, uint8_t uidLength) {
    String r;
    for (uint8_t i = 0; i < uidLength; i++) {
        if (i) r += ":";
        if (uid[i] < 0x10) r += "0";
        r += String(uid[i], HEX);
    }
    r.toUpperCase();
    return r;
}

void blinkLed(int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

// =============================================================================
// SSDP DISCOVERY
// =============================================================================

bool discoverDlnaRenderer() {
    log("SSDP: Searching for Yamaha...");

    const uint16_t localPort = 1901;
    WiFiUDP udp;
    if (!udp.begin(localPort)) {
        log("SSDP: udp.begin failed");
        return false;
    }

    // M-SEARCH multicast
    const char* msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "\r\n";

    IPAddress ssdpAddr(239, 255, 255, 250);
    udp.beginPacket(ssdpAddr, 1900);
    udp.print(msearch);
    udp.endPacket();

    log("SSDP: M-SEARCH sent, waiting for responses...");

    unsigned long start = millis();
    int packetsReceived = 0;
    while (millis() - start < SSDP_DISCOVER_TIMEOUT) {
        int packetSize = udp.parsePacket();
        if (packetSize > 0) {
            packetsReceived++;
            char buf[1536];
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len < 0) len = 0;
            buf[len] = '\0';
            String response = String(buf);
            IPAddress remoteIP = udp.remoteIP();
            log("SSDP: response from " + remoteIP.toString() + " (" + String(len) + "B)");

            // Szukamy naszej Yamahy po UUID (case-insensitive dla BOOTID itp.)
            if (response.indexOf("FFB8F002-F478-76B8-9813-5248FFB8F002") >= 0) {
                // Parsuj LOCATION header
                int locIdx = response.indexOf("LOCATION:");
                if (locIdx < 0) locIdx = response.indexOf("Location:");
                if (locIdx < 0) locIdx = response.indexOf("location:");
                if (locIdx >= 0) {
                    int urlStart = response.indexOf("http://", locIdx);
                    int urlEnd = response.indexOf("\r\n", urlStart);
                    String locationUrl = response.substring(urlStart, urlEnd);
                    locationUrl.trim();

                    // Parsuj host i port z URL: http://host:port/...
                    int hostStart = 7; // length of "http://"
                    String hostPort = locationUrl.substring(hostStart);
                    int slashIdx = hostPort.indexOf('/');
                    if (slashIdx > 0) hostPort = hostPort.substring(0, slashIdx);

                    int colonIdx = hostPort.indexOf(':');
                    if (colonIdx > 0) {
                        dlnaHost = hostPort.substring(0, colonIdx);
                        dlnaPort = hostPort.substring(colonIdx + 1).toInt();
                    } else {
                        dlnaHost = hostPort;
                        dlnaPort = 80;
                    }

                    dlnaFound = true;
                    log("SSDP: Yamaha found at " + dlnaHost + ":" + String(dlnaPort));
                    udp.stop();
                    return true;
                }
            }
        }
        delay(10);
    }

    log("SSDP: Yamaha not found (received " + String(packetsReceived) + " packets)");
    udp.stop();

    // Fallback: spróbuj bezpośrednio pod znanym ostatnim IP jeśli było
    if (dlnaHost.length() > 0) {
        log("SSDP: fallback to last known IP " + dlnaHost);
        return true;
    }
    return false;
}

// =============================================================================
// DLNA / UPnP
// =============================================================================

// Wysyła komendę SOAP do Yamahy przez HTTPClient (czeka na odpowiedź).
bool dlnaSendCommand(const char* controlUrl, const char* serviceType,
                     const char* action, const String& actionBody) {
    String soapBody = String(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:") + action + " xmlns:u=\"" + serviceType + "\">" +
        actionBody +
        "</u:" + action + ">"
        "</s:Body>"
        "</s:Envelope>";

    String soapAction = String("\"") + serviceType + "#" + action + "\"";

    logPrint("DLNA ");
    logPrint(action);

    HTTPClient http;
    String url = String("http://") + dlnaHost + ":" + dlnaPort + controlUrl;
    http.begin(url);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", soapAction);
    http.setTimeout(5000);

    int httpCode = http.POST(soapBody);
    http.end();

    if (httpCode == 200) {
        log(" - OK");
        return true;
    }

    log(" - HTTP error: " + String(httpCode));

    // Jeśli połączenie nie udane, spróbuj rediscovery
    if (httpCode < 0) {
        log("Retrying discovery...");
        dlnaFound = false;
        if (discoverDlnaRenderer()) {
            http.begin(String("http://") + dlnaHost + ":" + dlnaPort + controlUrl);
            http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
            http.addHeader("SOAPAction", soapAction);
            http.setTimeout(5000);
            httpCode = http.POST(soapBody);
            http.end();
            if (httpCode == 200) {
                log(" - OK after rediscovery");
                return true;
            }
        }
    }
    return false;
}

void dlnaSetAVTransportURI(const String& streamUrl) {
    // Escapuj znaki specjalne XML w URL
    String escapedUrl = streamUrl;
    escapedUrl.replace("&", "&amp;");

    String body = "<InstanceID>0</InstanceID>"
                  "<CurrentURI>" + escapedUrl + "</CurrentURI>"
                  "<CurrentURIMetaData></CurrentURIMetaData>";

    dlnaSendCommand(DLNA_TRANSPORT_URL,
                    "urn:schemas-upnp-org:service:AVTransport:1",
                    "SetAVTransportURI", body);
}

void dlnaPlay() {
    String body = "<InstanceID>0</InstanceID><Speed>1</Speed>";
    dlnaSendCommand(DLNA_TRANSPORT_URL,
                    "urn:schemas-upnp-org:service:AVTransport:1",
                    "Play", body);
}

void dlnaStop() {
    String body = "<InstanceID>0</InstanceID>";
    dlnaSendCommand(DLNA_TRANSPORT_URL,
                    "urn:schemas-upnp-org:service:AVTransport:1",
                    "Stop", body);
}

// Sprawdza czy Yamaha jest dostępna
bool dlnaCheckConnection() {
    String body = "<InstanceID>0</InstanceID>";
    return dlnaSendCommand(DLNA_TRANSPORT_URL,
        "urn:schemas-upnp-org:service:AVTransport:1",
        "GetTransportInfo", body);
}

// =============================================================================
// NFC
// =============================================================================

void reinitNfc() {
    nfc.begin();
    delay(500);
    if (nfc.getFirmwareVersion()) {
        nfc.SAMConfig();
        nfcReady = true;
        nfcErrorCount = 0;
    } else {
        nfcReady = false;
    }
}

String readNfcTag() {
    if (!nfcReady) {
        if (++nfcErrorCount > NFC_ERROR_THRESHOLD) {
            reinitNfc();
        }
        return "";
    }

    uint8_t uid[7];
    uint8_t uidLength;

    Serial.print(".");  // tylko Serial - za częste dla WebSerial

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
        noTagCount = 0;
        String uidStr = uidToString(uid, uidLength);
        log("NFC Tag: " + uidStr);
        return uidStr;
    }
    return "";
}

// =============================================================================
// MUSICBOX SERVER
// =============================================================================

String getStreamUrl(String uid) {
    HTTPClient http;
    String url = "http://" + String(SERVER_HOST) + ":" + SERVER_PORT + "/api/play/" + uid;
    log("Fetching: " + url);

    http.begin(url);
    http.setReuse(false);
    http.setTimeout(5000);

    String result = "";
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            const char* trackTitle = doc["track_title"];
            const char* figurineName = doc["figurine_name"];
            result = String((const char*)doc["stream_url"]);

            log("Figurka: " + String(figurineName ? figurineName : "nieznana"));
            log("Utwor: " + String(trackTitle ? trackTitle : "brak"));
            log("Stream: " + result);
        }
    } else if (httpCode == 404) {
        log("Brak figurki lub utworu dla tego NFC!");
    } else {
        log("HTTP error: " + String(httpCode));
    }

    http.end();
    return result;
}

// =============================================================================
// PLAYBACK
// =============================================================================

void startPlayback(String uid) {
    log("--- Rozpoczynam odtwarzanie ---");

    if (!dlnaFound) {
        log("DLNA nie znalezione, szukam...");
        if (!discoverDlnaRenderer()) {
            log("Nie znaleziono Yamahy!");
            return;
        }
    }

    String url = getStreamUrl(uid);
    if (!url.isEmpty()) {
        dlnaSetAVTransportURI(url);
        delay(200);
        dlnaPlay();
        isPlaying = true;
        lastNfcUid = uid;
    } else {
        log("Nie mozna odtworzyc - brak URL");
    }
}

void stopPlayback() {
    log("--- Stop odtwarzania ---");
    dlnaStop();
    isPlaying = false;
    lastNfcUid = "";
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);

    if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0) {
        Serial.println("WiFi credentials are not configured.");
        Serial.println("Set WIFI_SSID and WIFI_PASSWORD in private PlatformIO build flags.");
        while (true) {
            blinkLed(1, 500);
        }
    }

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    // OTA
    ArduinoOTA.setHostname("musicbox");
    ArduinoOTA.onStart([]() {
        log("OTA: Update starting...");
    });
    ArduinoOTA.onEnd([]() {
        log("OTA: Update complete!");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        logPrint("OTA Error: ");
        log(String(error));
    });
    ArduinoOTA.begin();
    Serial.println("OTA ready");

    // WebSerial - debug przez http://musicbox.local/webserial
    WebSerial.begin(&webServer);
    webServer.begin();
    Serial.println("WebSerial ready at http://musicbox.local/webserial");

    // NFC
    log("Initializing NFC (SPI)...");
    nfc.begin();
    delay(1000);

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
        log("PN532 found!");
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);
        nfcReady = true;
        log("NFC ready!");
    } else {
        log("ERROR: PN532 not found!");
        blinkLed(10, 100);
    }

    // DLNA - wyszukaj Yamahę przez SSDP
    log("Searching for Yamaha via SSDP...");
    if (discoverDlnaRenderer()) {
        blinkLed(2, 200);
    } else {
        log("WARNING: Yamaha not found. Will retry on playback.");
        blinkLed(5, 100);
    }

    log("\nReady! Place NFC tag to play music.");
    log("IP: " + WiFi.localIP().toString());
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    ArduinoOTA.handle();

    if (millis() - lastNfcRead > NFC_READ_INTERVAL) {
        lastNfcRead = millis();

        currentNfcUid = readNfcTag();
        if (!currentNfcUid.isEmpty()) {
            noTagCount = 0;
            if (currentNfcUid != lastNfcUid) {
                startPlayback(currentNfcUid);
            }
        } else if (isPlaying && ++noTagCount >= NO_TAG_THRESHOLD) {
            stopPlayback();
            noTagCount = 0;
        }
    }
}
