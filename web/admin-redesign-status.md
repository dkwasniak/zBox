# zBox Admin Panel — Status redesignu

## Co zostało zrobione

Zastąpiono stary panel Alpine.js + Tailwind nowym panelem React, zgodnym z designem z Claude Design.

### Pliki w `server/web/`

| Plik | Status | Uwagi |
|------|--------|-------|
| `index.html` | ✅ zastąpiony | React + Babel standalone, ładuje wszystkie JSX |
| `styles.css` | ✅ nowy | Pełny design system (tokeny, komponenty) — naprawiony bug CSS z `.sync-banner` |
| `tweaks-panel.jsx` | ✅ skopiowany | Bez zmian z designu (panel "Tweaks") |
| `icons.jsx` | ✅ skopiowany | Bez zmian z designu (zestaw ikon Lucide-style) |
| `data.jsx` | ✅ nowy | API helpers, `fmtDate`, `fmtBytes`, `SOUND_SLOT_META`, `buildFileTree`, `waveBars` |
| `app.jsx` | ✅ nowy | AppCtx, routing, NowPlayingBar z prawdziwym Audio API |
| `section-dashboard.jsx` | ✅ nowy | Prawdziwe dane z API, sync check |
| `section-songs.jsx` | ✅ nowy | Upload MP3, YouTube import z pollingiem, delete, trim UI |
| `section-tags.jsx` | ✅ nowy | Figurki jako tagi, przypisanie, dodawanie ręczne |
| `section-device.jsx` | ✅ nowy | Status urządzenia, sync flow z pollingiem, SD files tree, IP settings |
| `section-sounds.jsx` | ✅ nowy | 8 slotów systemowych z upload/delete |

### Podłączone endpointy API

- `GET/POST/DELETE /admin/tracks` — biblioteka
- `POST /admin/tracks/youtube` + polling statusu — import YouTube
- `POST /admin/tracks/{id}/trim` — przycinanie
- `GET/POST/DELETE /admin/figurines` + `PUT /admin/figurines/{id}` — tagi NFC
- `GET/POST/DELETE /admin/system_sounds/{name}` — dźwięki systemowe
- `GET /admin/devices` — status urządzenia (polling co 30s)
- `GET /admin/device-settings` + `POST` — zapisanie IP
- `GET /admin/devices/{id}/files` — pliki SD
- `GET /admin/devices/{id}/sync/check` — stan synchronizacji
- `POST /admin/devices/{id}/sync` + polling — sync flow
- `POST /admin/devices/{id}/restart` — restart

---

## Co jest do weryfikacji / ewentualnego poprawienia

1. **Nie przetestowane na żywym serwerze** — trzeba wdrożyć na RPi i sprawdzić:
   - Czy Babel ładuje pliki JSX przez względne URL-e poprawnie
   - Czy `AppCtx` (eksportowany przez `window.AppCtx = AppCtx`) jest dostępny w sekcjach
   - Czy `apiFetch` działa z względnymi ścieżkami (`/admin/...`)

2. **Trim editor** — używa `window._trimDuration` (nigdzie nie ustawianego) do wyświetlania czasu.
   Trzeba albo pobrać duration z pliku audio, albo uprościć do pokazywania % zamiast MM:SS.

3. **NowPlayingBar** — odtwarza przez `/api/stream/file/{filename}`. Jeśli serwer wymaga auth lub plik nie istnieje, audio cicho nie gra. Warto dodać obsługę błędu.

4. **Sync task po odświeżeniu strony** — task jest w pamięci (`dict` w Pythonie), po F5 jest tracony. To samo zachowanie co poprzednio — można polling uruchomić przez `/admin/devices/{id}/sync/current`.

5. **Sound slot "Przywróć domyślne"** — przycisk usuwa wszystkie ustawione dźwięki. Brak prawdziwych "domyślnych" plików w API — to tylko reset do stanu pustego.

6. **Responsive < 1100px** — design ma `viewport=1280`, więc małe ekrany są przeskalowane przez browser zoom.

7. **TweaksPanel** — panel designu (prawy dolny róg) uruchamia się przez wiadomość `__activate_edit_mode` od rodzica, nie jest widoczny normalnie. To cecha designu — można dodać skrót klawiaturowy lub ukryty przycisk jeśli potrzebny.

---

## Deployment

```bash
rsync -avz --exclude '__pycache__' --exclude '*.pyc' --exclude '.git' \
  <repo>/ rpi@<musicbox-server>:~/musicbox/

ssh rpi@<musicbox-server> "cd ~/musicbox && docker compose up -d --build"
```
