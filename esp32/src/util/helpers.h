#pragma once
#include <Arduino.h>
#include <ctype.h>     // isalnum()
#include <stdint.h>

inline String urlEncode(const String &str)
{
    const char *hex = "0123456789ABCDEF";
    String encoded;
    for (unsigned int i = 0; i < str.length(); i++)
    {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
        {
            encoded += c;
        }
        else
        {
            uint8_t b = (uint8_t)c;
            encoded += '%';
            encoded += hex[b >> 4];
            encoded += hex[b & 0x0F];
        }
    }
    return encoded;
}

inline String uidToString(uint8_t *uid, uint8_t uidLength)
{
    String r;
    for (uint8_t i = 0; i < uidLength; i++)
    {
        if (i)
            r += ":";
        if (uid[i] < 0x10)
            r += "0";
        r += String(uid[i], HEX);
    }
    r.toUpperCase();
    return r;
}

// Heap-free variant: writes "AA:BB:CC..." into buf, returns true on success.
inline bool uidToBuffer(const uint8_t *uid, uint8_t uidLength, char *buf, size_t bufSize)
{
    if (!buf || bufSize == 0) return false;
    buf[0] = '\0';
    size_t pos = 0;
    for (uint8_t i = 0; i < uidLength; i++) {
        const char sep = (i > 0) ? ':' : 0;
        const int n = sep
            ? snprintf(buf + pos, bufSize - pos, ":%02X", uid[i])
            : snprintf(buf + pos, bufSize - pos, "%02X", uid[i]);
        if (n < 0 || (size_t)n >= bufSize - pos) { buf[0] = '\0'; return false; }
        pos += (size_t)n;
    }
    return true;
}

// Thresholds from the real Li-Po 1S discharge curve:
// 4.20V=100%, 3.90V=~60%, 3.80V=~40%, 3.70V=~20%, <3.50V=critical
// 5=80-100%, 4=60-80%, 3=40-60%, 2=20-40%, 1=<20%
inline int batteryBars(float v)
{
    if (v >= 4.05f) return 5;
    if (v >= 3.90f) return 4;
    if (v >= 3.80f) return 3;
    if (v >= 3.70f) return 2;
    return 1;
}

inline const char *batteryColorName(int bars)
{
    if (bars >= 5) return "blue";
    if (bars == 4) return "green";
    if (bars == 3) return "yellow";
    if (bars == 2) return "orange";
    return "red";
}
