# DLNA - Sterowanie Yamaha YAS-209

## Opis

ESP32 steruje soundbarem Yamaha YAS-209 bezpośrednio przez DLNA/UPnP (SOAP over HTTP).
Wcześniej używaliśmy Music Assistant jako pośrednika — teraz ESP32 komunikuje się z Yamahą bezpośrednio.

## Połączenie

- **Yamaha YAS-209**: `http://<yamaha-ip>:49152`
- **Serwer MusicBox (streamy)**: `http://<musicbox-server>:8000`

## UPnP Endpointy Yamahy

| Endpoint | Serwis | Opis |
|----------|--------|------|
| `/upnp/control/rendertransport1` | AVTransport:1 | Odtwarzanie (play, stop, set URI) |
| `/upnp/control/rendercontrol1` | RenderingControl:1 | Głośność |
| `/upnp/control/renderconnmgr1` | ConnectionManager:1 | Info o połączeniach |
| `/description.xml` | - | Opis urządzenia UPnP |

## Komendy SOAP

Wszystkie requesty: `POST http://<yamaha-ip>:49152/<endpoint>` z headerami:
- `Content-Type: text/xml; charset="utf-8"`
- `SOAPAction: "<serviceType>#<action>"`

### SetAVTransportURI - ustaw co odtwarzać

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendertransport1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
      <CurrentURI>http://<musicbox-server>:8000/api/stream/2</CurrentURI>
      <CurrentURIMetaData></CurrentURIMetaData>
    </u:SetAVTransportURI>
  </s:Body>
</s:Envelope>'
```

### Play

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendertransport1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#Play"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
      <Speed>1</Speed>
    </u:Play>
  </s:Body>
</s:Envelope>'
```

### Stop

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendertransport1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#Stop"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Stop xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:Stop>
  </s:Body>
</s:Envelope>'
```

### GetTransportInfo - sprawdź stan odtwarzania

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendertransport1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:GetTransportInfo>
  </s:Body>
</s:Envelope>'
```

Zwraca `CurrentTransportState`: `STOPPED`, `PLAYING`, `PAUSED_PLAYBACK`, `TRANSITIONING`, `NO_MEDIA_PRESENT`.

### SetVolume (0-100)

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendercontrol1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:RenderingControl:1#SetVolume"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:SetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
      <InstanceID>0</InstanceID>
      <Channel>Master</Channel>
      <DesiredVolume>30</DesiredVolume>
    </u:SetVolume>
  </s:Body>
</s:Envelope>'
```

### GetVolume

```bash
curl -s -X POST http://<yamaha-ip>:49152/upnp/control/rendercontrol1 \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:RenderingControl:1#GetVolume"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
      <InstanceID>0</InstanceID>
      <Channel>Master</Channel>
    </u:GetVolume>
  </s:Body>
</s:Envelope>'
```

## SSDP Discovery

Yamaha odpowiada na multicast SSDP (239.255.255.250:1900):

```
ST: urn:schemas-upnp-org:device:MediaRenderer:1
USN: uuid:<renderer-uuid>
LOCATION: http://<yamaha-ip>:49152/description.xml
```

## Notatki

- Yamaha obsługuje streamy MP3 przez HTTP (audio/mpeg)
- SetAVTransportURI + Play to dwa osobne requesty — potrzebna krótka pauza (~200ms) między nimi
- Fire-and-forget pattern: ESP32 wysyła SOAP przez raw TCP socket i nie czeka na odpowiedź (szybsze niż HTTPClient)
- Yamaha może zmienić IP po restarcie routera — w razie problemów sprawdź przez SSDP discovery
