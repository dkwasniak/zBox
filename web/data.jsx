// API utilities, i18n helpers, and shared formatting for the zBox admin panel

const API_BASE = "";
const LOCALE_STORAGE_KEY = "zbox_locale";
const DEFAULT_LOCALE = "en";
const LOCALE_TAGS = { en: "en-US", pl: "pl-PL" };

const TRANSLATIONS = {
  en: {
    common: {
      dashboard: "Dashboard",
      songs: "Tracks",
      tags: "NFC Tags",
      device: "Device",
      sounds: "System Sounds",
      library: "Library",
      signals: "Signals",
      look: "Look",
      sections: "Sections",
      palette: "Palette",
      softness: "Corners",
      density: "Density",
      comfortable: "Comfortable",
      dense: "Dense",
      showLed: "Show LED section (soon)",
      lightMode: "Light mode",
      darkMode: "Dark mode",
      loading: "Loading...",
      noData: "No data",
      none: "None",
      online: "Online",
      offline: "Offline",
      openDevice: "Open device",
      details: "View details",
      syncNow: "Sync now",
      retry: "Try again",
      save: "Save",
      saveAndCheck: "Save and check",
      check: "Check",
      cancel: "Cancel",
      replace: "Replace",
      upload: "Upload file",
      uploadTrack: "Add track",
      delete: "Delete",
      restart: "Restart",
      refresh: "Refresh",
      files: "Files",
      preview: "Preview",
      download: "Download",
      actions: "Actions",
      file: "File",
      title: "Title",
      added: "Added",
      close: "Close",
      play: "Play",
      unassigned: "Unassigned",
      empty: "Empty",
      assigned: "Assigned",
      ready: "Ready",
      waiting: "Waiting...",
      read: "Read",
      selected: "Selected",
      track: "Track",
      ipAddress: "Device IP address",
      hostname: "Hostname",
      mode: "Mode",
      battery: "Battery",
      connection: "Connection",
      sdCard: "SD card",
      status: "Status",
      uploaded: "Uploaded",
      deletedFiles: "Deleted",
    },
    app: {
      brandSub: "family · audio",
      soon: "Soon",
      noConnection: "no connection",
      footerDeviceName: "zBox",
      browseAllTags: "All tags",
      browseLibrary: "Library",
      emptyDate: "No date",
      noFile: "No file",
      tailFragment: "File tail shown",
      fullFragment: "Full returned fragment",
      logPreview: "Log preview",
      noLogs: "No logs",
      logLines: ({ value }) => `${value} lines`,
      language: "Language",
    },
    dashboard: {
      greeting: "Good morning",
      offlineLead: "zBox is offline right now. You can keep preparing changes and sync them the next time it connects.",
      onlineNeedsAttention: "zBox is online. A few things need attention.",
      onlineUpToDate: "zBox is online. Everything is in sync.",
      offlineTitle: "zBox is offline",
      offlineBody: "Check power and Wi-Fi on the device. Changes will sync automatically after it reconnects.",
      pendingChanges: ({ count }) => `${count} ${count === 1 ? "change is" : "changes are"} waiting to upload`,
      tracksInLibrary: ({ count }) => count === 0 ? "no tracks yet" : `${count} in the library`,
      tagsUnassigned: ({ count }) => `${count} unassigned`,
      systemSoundSlots: ({ count }) => `${count} empty slots`,
      tagsWithoutTrack: "Tags without a track",
      items: ({ count }) => `${count} ${count === 1 ? "item" : "items"}`,
      allTagsAssigned: "All tags already have tracks assigned",
      assignTrack: "Assign track",
      nfcFigurine: "NFC figurine",
      recentlyAdded: "Recently added",
      libraryEmpty: "The library is empty",
      trimmed: "trimmed",
    },
    songs: {
      heading: "Tracks",
      description: "Your library. Add files or YouTube audio, trim clips, and assign tracks to tags.",
      onlyMp3: "Only MP3 files are allowed",
      promptTrackTitle: "Track title:",
      confirmDelete: ({ title }) => `Delete "${title}"?`,
      all: "All",
      trimmed: "Trimmed",
      emptyLibrary: "The library is empty. Add your first track.",
      noResults: "No results.",
      dropTitle: "Drop an MP3 file here",
      dropTitleUploading: "Uploading...",
      dropHint: "or click to choose a file · MP3 only",
      youtubeImport: "Import from YouTube",
      inProgress: "in progress",
      done: "done",
      error: "error",
      youtubeLink: "YouTube link",
      trackTitle: "Track title",
      exampleTitle: "for example: Dragon Song",
      addedToLibrary: "Added to the library",
      importError: "Import failed",
      importBackground: "Import runs in the background. You can keep working.",
      importButton: "Import from YouTube",
      trimming: "Trimming",
      previewSelection: "Preview selection",
      stop: "Stop",
      start: "Start",
      length: "Length",
      end: "End",
      startSeconds: "Start (seconds)",
      endSeconds: "End (seconds)",
      saveTrim: "Save trim",
      saving: "Saving...",
      trimEditor: "Trim editor",
      trimEditorHint: "Click the scissors icon on a track to trim it",
      trimAction: "Trim",
      deleteAction: "Delete",
    },
    tags: {
      heading: "NFC Tags",
      description: "Cards and figurines the child places on the device. Each tag can have one assigned track.",
      withTrack: "With track",
      empty: "Empty",
      tagName: "Tag name",
      assignedTrack: "Assigned track",
      noTags: "No NFC tags yet. Add the first one.",
      noAssignment: "No assignment",
      quickAssign: "Quick assign",
      tagOrFigurine: "Tag / figurine",
      chooseTag: "choose a tag",
      removeAssignment: "remove assignment",
      syncAfterSave: "Run sync after saving so the change reaches the device.",
      assignTrack: "Assign track",
      addManually: "Add tag manually",
      name: "Name",
      addTag: "Add tag",
      confirmDelete: ({ name }) => `Delete tag "${name}"?`,
      scanNfc: "Scan NFC",
      requiresHttps: "requires HTTPS",
      androidOnly: "Android only",
      webNfcHttps: "Web NFC requires HTTPS. In Chrome on Android open:",
      webNfcOnly: "Web NFC currently works only in Chrome on Android. On iPhone, enter the UID manually.",
      scanTag: "Scan NFC tag",
      holdToPhone: "Hold the figurine near the phone",
      knownFigurine: ({ name, track }) => `Known figurine: ${name}${track ? ` -> ${track}` : " (no track assigned)"}`,
      newTagHint: "New tag. The UID was filled into the form below. Enter a name and add it.",
      scanAgain: "Scan again",
      scanRetry: "Try again",
      scanError: "NFC scan failed",
    },
    device: {
      heading: "Device and sync",
      description: "Current zBox state, SD card contents, and syncing changes to the device.",
      syncStarting: "Starting...",
      syncing: "Syncing...",
      ipSettings: "Connection settings",
      offlineTitle: "We cannot see zBox on the network",
      offlineBody: "This is normal if the device is powered off, out of Wi-Fi range, or got a new IP address. Changes will sync after the connection returns.",
      whatToCheck: "Things to check",
      power: "Power",
      powerHint: "Make sure zBox is turned on and charged.",
      wifi: "Wi-Fi",
      wifiHint: "The device and this computer must be on the same network.",
      ipHint: "If the router assigned a new address, update it below.",
      pingOk: "ping OK",
      readyToUse: "ready to use",
      noSdData: "no data",
      deviceCard: "Device",
      signalGood: "good",
      signalWeak: "weak",
      signalVeryWeak: "very weak",
      inUse: "in use",
      freeOf: ({ free, total }) => `${free} free of ${total}`,
      syncCard: "Sync",
      syncPrompt: 'Click "Sync now" to upload changes to the device.',
      syncReady: "Sync complete",
      syncError: "Sync failed",
      syncRunning: "Sync in progress",
      pleaseWait: "Please wait...",
      unknownError: "Unknown error",
      syncAgain: "Sync again",
      checkChanges: "Check changes",
      tagMappings: "NFC tag mappings",
      uploadFiles: "Transfer files",
      systemSounds: "System sounds",
      sdFiles: "Files on the SD card",
      contents: "Contents",
      logFiles: "Diagnostic logs",
      logFile: "Log file",
      fileTail: "Tail",
      cardUnavailable: "SD card unavailable",
      noLogData: "No log data to display.",
      loadingLog: "Loading log...",
      restartConfirm: "Restart the device?",
      syncErrorAlert: ({ message }) => `Sync error: ${message}`,
      restartErrorAlert: ({ message }) => `Restart error: ${message}`,
      saveErrorAlert: ({ message }) => `Save error: ${message}`,
      filesWord: ({ count }) => `${count} ${count === 1 ? "file" : "files"}`,
      tagsWord: ({ count }) => `${count} ${count === 1 ? "tag" : "tags"}`,
      slotsWord: ({ count }) => `${count} ${count === 1 ? "slot" : "slots"}`,
    },
    sounds: {
      heading: "System Sounds",
      description: "Signals played by the device during key events. Each slot represents one specific event.",
      resetConfirm: "Remove all assigned system sounds?",
      resetButton: "Reset to firmware defaults",
      summary: ({ setCount, total }) => `${setCount} of ${total} slots have an assigned sound. Empty slots fall back to firmware behavior.`,
      confirmDelete: ({ label }) => `Delete sound "${label}"?`,
      assigned: "assigned",
      empty: "empty",
      preview: "Preview",
      clickOrDrop: "Drop an MP3 file or click",
      uploading: "Uploading...",
      replace: "Replace",
    },
    soundSlots: {
      startup: "Device startup",
      power_off: "Device power off",
      nfc_mode: "NFC mode",
      music_mode: "Music mode",
    },
  },
  pl: {
    common: {
      dashboard: "Pulpit",
      songs: "Utwory",
      tags: "Tagi NFC",
      device: "Urządzenie",
      sounds: "Dźwięki systemowe",
      library: "Biblioteka",
      signals: "Sygnały",
      look: "Wygląd",
      sections: "Sekcje",
      palette: "Paleta",
      softness: "Zaokrąglenia",
      density: "Gęstość",
      comfortable: "Wygodnie",
      dense: "Gęsto",
      showLed: "Pokaż sekcję LED (wkrótce)",
      lightMode: "Tryb jasny",
      darkMode: "Tryb ciemny",
      loading: "Ładowanie...",
      noData: "Brak danych",
      none: "Brak",
      online: "Online",
      offline: "Offline",
      openDevice: "Otwórz urządzenie",
      details: "Zobacz szczegóły",
      syncNow: "Synchronizuj teraz",
      retry: "Spróbuj ponownie",
      save: "Zapisz",
      saveAndCheck: "Zapisz i sprawdź",
      check: "Sprawdź",
      cancel: "Anuluj",
      replace: "Podmień",
      upload: "Wgraj plik",
      uploadTrack: "Dodaj utwór",
      delete: "Usuń",
      restart: "Restart",
      refresh: "Odśwież",
      files: "Pliki",
      preview: "Podgląd",
      download: "Pobierz",
      actions: "Akcje",
      file: "Plik",
      title: "Tytuł",
      added: "Dodano",
      close: "Zamknij",
      play: "Odtwórz",
      unassigned: "Brak przypisania",
      empty: "Puste",
      assigned: "Przypisane",
      ready: "Gotowe",
      waiting: "Czekam...",
      read: "Odczytano",
      selected: "Zaznaczenie",
      track: "Utwór",
      ipAddress: "Adres IP urządzenia",
      hostname: "Host",
      mode: "Tryb",
      battery: "Bateria",
      connection: "Połączenie",
      sdCard: "Karta SD",
      status: "Status",
      uploaded: "Przesłane",
      deletedFiles: "Usunięte",
    },
    app: {
      brandSub: "family · audio",
      soon: "Wkrótce",
      noConnection: "brak połączenia",
      footerDeviceName: "zBox",
      browseAllTags: "Wszystkie tagi",
      browseLibrary: "Biblioteka",
      emptyDate: "brak daty",
      noFile: "Brak pliku",
      tailFragment: "Pokazano końcówkę pliku",
      fullFragment: "Pełny zwrócony fragment",
      logPreview: "Podgląd logu",
      noLogs: "Brak logów",
      logLines: ({ value }) => `${value} linii`,
      language: "Język",
    },
    dashboard: {
      greeting: "Dzień dobry",
      offlineLead: "zBox jest teraz offline. Możesz spokojnie przygotować zmiany i zsynchronizować je przy następnym połączeniu.",
      onlineNeedsAttention: "zBox jest online. Kilka rzeczy czeka na uwagę.",
      onlineUpToDate: "zBox jest online. Wszystko jest zsynchronizowane.",
      offlineTitle: "zBox jest offline",
      offlineBody: "Sprawdź zasilanie i Wi-Fi urządzenia. Zmiany zostaną zsynchronizowane automatycznie po połączeniu.",
      pendingChanges: ({ count }) => `${count} ${count === 1 ? "zmiana czeka" : "zmiany czekają"} na wgranie`,
      tracksInLibrary: ({ count }) => count === 0 ? "brak utworów" : `${count} w bibliotece`,
      tagsUnassigned: ({ count }) => `${count} bez przypisania`,
      systemSoundSlots: ({ count }) => `${count} slotów pustych`,
      tagsWithoutTrack: "Tagi bez utworu",
      items: ({ count }) => `${count} ${count === 1 ? "pozycja" : "pozycji"}`,
      allTagsAssigned: "Wszystkie tagi mają przypisane utwory",
      assignTrack: "Przypisz utwór",
      nfcFigurine: "Figurka NFC",
      recentlyAdded: "Ostatnio dodane",
      libraryEmpty: "Brak utworów w bibliotece",
      trimmed: "przycięty",
    },
    songs: {
      heading: "Utwory",
      description: "Twoja biblioteka. Dodawaj pliki lub YouTube, przycinaj fragmenty i przypisuj utwory do tagów.",
      onlyMp3: "Dozwolone tylko pliki MP3",
      promptTrackTitle: "Tytuł utworu:",
      confirmDelete: ({ title }) => `Usunąć "${title}"?`,
      all: "Wszystkie",
      trimmed: "Przycięte",
      emptyLibrary: "Biblioteka jest pusta. Dodaj pierwszy utwór.",
      noResults: "Brak wyników.",
      dropTitle: "Upuść plik MP3 tutaj",
      dropTitleUploading: "Wgrywanie...",
      dropHint: "albo kliknij, aby wybrać · tylko MP3",
      youtubeImport: "Import z YouTube",
      inProgress: "w toku",
      done: "gotowe",
      error: "błąd",
      youtubeLink: "Link YouTube",
      trackTitle: "Tytuł utworu",
      exampleTitle: "np. Piosenka o smoku",
      addedToLibrary: "Dodano do biblioteki",
      importError: "Błąd importu",
      importBackground: "Import działa w tle. Możesz spokojnie kontynuować pracę.",
      importButton: "Importuj z YouTube",
      trimming: "Przycinanie",
      previewSelection: "Odsłuchaj zaznaczenie",
      stop: "Zatrzymaj",
      start: "Początek",
      length: "Długość",
      end: "Koniec",
      startSeconds: "Początek (sekundy)",
      endSeconds: "Koniec (sekundy)",
      saveTrim: "Zapisz przycięcie",
      saving: "Zapisywanie...",
      trimEditor: "Edytor przycinania",
      trimEditorHint: "Kliknij ikonę nożyczek przy wybranym utworze, aby go przyciąć",
      trimAction: "Przytnij",
      deleteAction: "Usuń",
    },
    tags: {
      heading: "Tagi NFC",
      description: "Karty i figurki, które dziecko przykłada do urządzenia. Każdy tag może mieć jeden przypisany utwór.",
      withTrack: "Z utworem",
      empty: "Puste",
      tagName: "Nazwa tagu",
      assignedTrack: "Przypisany utwór",
      noTags: "Brak tagów NFC. Dodaj pierwszy tag.",
      noAssignment: "Brak przypisania",
      quickAssign: "Szybkie przypisanie",
      tagOrFigurine: "Tag / figurka",
      chooseTag: "wybierz tag",
      removeAssignment: "usuń przypisanie",
      syncAfterSave: "Po zapisie uruchom synchronizację, aby zmiany trafiły na urządzenie.",
      assignTrack: "Przypisz utwór",
      addManually: "Dodaj tag ręcznie",
      name: "Nazwa",
      addTag: "Dodaj tag",
      confirmDelete: ({ name }) => `Usunąć tag "${name}"?`,
      scanNfc: "Skanuj NFC",
      requiresHttps: "wymaga HTTPS",
      androidOnly: "tylko Android",
      webNfcHttps: "Web NFC wymaga HTTPS. W Chrome na Androidzie wejdź na:",
      webNfcOnly: "Web NFC działa tylko w Chrome na Androidzie. Na iPhone wpisz UID ręcznie.",
      scanTag: "Skanuj tag NFC",
      holdToPhone: "Przyłóż figurkę do telefonu",
      knownFigurine: ({ name, track }) => `Znana figurka: ${name}${track ? ` -> ${track}` : " (brak przypisanego utworu)"}`,
      newTagHint: "Nowy tag. UID uzupełniono w formularzu poniżej. Wpisz nazwę i dodaj.",
      scanAgain: "Skanuj ponownie",
      scanRetry: "Spróbuj ponownie",
      scanError: "Błąd skanowania NFC",
    },
    device: {
      heading: "Urządzenie i synchronizacja",
      description: "Stan zBoxa, zawartość karty SD i wgrywanie zmian na urządzenie.",
      syncStarting: "Uruchamianie...",
      syncing: "Synchronizacja...",
      ipSettings: "Ustawienia połączenia",
      offlineTitle: "Nie widzimy zBoxa w sieci",
      offlineBody: "To zwykła sytuacja. Urządzenie może być wyłączone, poza zasięgiem Wi-Fi albo otrzymało nowy adres IP. Zmiany zostaną zsynchronizowane po przywróceniu połączenia.",
      whatToCheck: "Co możesz sprawdzić",
      power: "Zasilanie",
      powerHint: "Upewnij się, że zBox jest włączony i naładowany.",
      wifi: "Wi-Fi",
      wifiHint: "Urządzenie i ten komputer muszą być w tej samej sieci.",
      ipHint: "Jeśli router przypisał nowy adres, zaktualizuj go poniżej.",
      pingOk: "ping OK",
      readyToUse: "gotowe do zabawy",
      noSdData: "brak danych",
      deviceCard: "Urządzenie",
      signalGood: "dobry",
      signalWeak: "słaby",
      signalVeryWeak: "bardzo słaby",
      inUse: "w użyciu",
      freeOf: ({ free, total }) => `${free} wolnego z ${total}`,
      syncCard: "Synchronizacja",
      syncPrompt: 'Kliknij "Synchronizuj teraz", aby wgrać zmiany na urządzenie.',
      syncReady: "Synchronizacja gotowa",
      syncError: "Błąd synchronizacji",
      syncRunning: "Synchronizacja w toku",
      pleaseWait: "Proszę czekać...",
      unknownError: "Nieznany błąd",
      syncAgain: "Synchronizuj ponownie",
      checkChanges: "Sprawdzenie zmian",
      tagMappings: "Mapowania tagów NFC",
      uploadFiles: "Przesyłanie plików",
      systemSounds: "Dźwięki systemowe",
      sdFiles: "Pliki na karcie SD",
      contents: "Zawartość",
      logFiles: "Logi diagnostyczne",
      logFile: "Plik logu",
      fileTail: "Tail",
      cardUnavailable: "Karta SD niedostępna",
      noLogData: "Brak danych do wyświetlenia.",
      loadingLog: "Ładowanie logu...",
      restartConfirm: "Zrestartować urządzenie?",
      syncErrorAlert: ({ message }) => `Błąd synchronizacji: ${message}`,
      restartErrorAlert: ({ message }) => `Błąd restartu: ${message}`,
      saveErrorAlert: ({ message }) => `Błąd zapisu: ${message}`,
      filesWord: ({ count }) => `${count} ${count === 1 ? "plik" : "pliki"}`,
      tagsWord: ({ count }) => `${count} ${count === 1 ? "tag" : "tagów"}`,
      slotsWord: ({ count }) => `${count} ${count === 1 ? "slot" : "slotów"}`,
    },
    sounds: {
      heading: "Dźwięki systemowe",
      description: "Sygnały odtwarzane przez urządzenie podczas zdarzeń. Każdy slot to jedno konkretne zdarzenie.",
      resetConfirm: "Usunąć wszystkie dźwięki systemowe?",
      resetButton: "Przywróć domyślne",
      summary: ({ setCount, total }) => `${setCount} z ${total} slotów ma przypisany dźwięk. Puste sloty wracają do zachowania z firmware.`,
      confirmDelete: ({ label }) => `Usunąć dźwięk "${label}"?`,
      assigned: "przypisany",
      empty: "pusty",
      preview: "Odsłuchaj",
      clickOrDrop: "Upuść plik MP3 lub kliknij",
      uploading: "Wgrywanie...",
      replace: "Podmień",
    },
    soundSlots: {
      startup: "Start urządzenia",
      power_off: "Wyłączenie urządzenia",
      nfc_mode: "Tryb NFC",
      music_mode: "Tryb Music",
    },
  },
};

const AppIntlCtx = React.createContext({
  locale: DEFAULT_LOCALE,
  setLocale: () => {},
  t: (key) => key,
});

function getLocaleTag(locale = DEFAULT_LOCALE) {
  return LOCALE_TAGS[locale] || LOCALE_TAGS[DEFAULT_LOCALE];
}

function getStoredLocale() {
  try {
    const value = localStorage.getItem(LOCALE_STORAGE_KEY);
    return value && TRANSLATIONS[value] ? value : DEFAULT_LOCALE;
  } catch {
    return DEFAULT_LOCALE;
  }
}

function getByPath(obj, path) {
  return path.split(".").reduce((acc, part) => (acc == null ? acc : acc[part]), obj);
}

function createTranslator(locale) {
  return (key, params = {}) => {
    const entry = getByPath(TRANSLATIONS[locale] || TRANSLATIONS[DEFAULT_LOCALE], key)
      ?? getByPath(TRANSLATIONS[DEFAULT_LOCALE], key);
    if (typeof entry === "function") return entry(params);
    return entry ?? key;
  };
}

function useI18n() {
  return React.useContext(AppIntlCtx);
}

async function apiFetch(path, opts = {}) {
  const res = await fetch(API_BASE + path, opts);
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(text || `HTTP ${res.status}`);
  }
  if (res.status === 204) return null;
  return res.json();
}

async function apiPost(path, body) {
  return apiFetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

async function apiDelete(path) {
  return apiFetch(path, { method: "DELETE" });
}

function fmtDate(iso, locale = DEFAULT_LOCALE) {
  if (!iso) return "—";
  const utc = /[Zz]|[+-]\d{2}:\d{2}$/.test(iso) ? iso : iso + "Z";
  const d = new Date(utc);
  const now = new Date();
  const diffSeconds = Math.round((d.getTime() - now.getTime()) / 1000);
  const abs = Math.abs(diffSeconds);
  if (abs < 60) {
    return new Intl.RelativeTimeFormat(getLocaleTag(locale), { numeric: "auto" }).format(0, "second");
  }
  if (abs < 3600) {
    return new Intl.RelativeTimeFormat(getLocaleTag(locale), { numeric: "auto" }).format(Math.round(diffSeconds / 60), "minute");
  }
  if (abs < 86400) {
    return new Intl.RelativeTimeFormat(getLocaleTag(locale), { numeric: "auto" }).format(Math.round(diffSeconds / 3600), "hour");
  }
  if (abs < 604800) {
    return new Intl.RelativeTimeFormat(getLocaleTag(locale), { numeric: "auto" }).format(Math.round(diffSeconds / 86400), "day");
  }
  return new Intl.DateTimeFormat(getLocaleTag(locale), { dateStyle: "medium" }).format(d);
}

function fmtDateTime(value, locale = DEFAULT_LOCALE) {
  if (!value) return "—";
  const d = value instanceof Date ? value : new Date(value);
  return new Intl.DateTimeFormat(getLocaleTag(locale), {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(d);
}

function fmtBytes(bytes) {
  if (!bytes) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1048576) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / 1048576).toFixed(1)} MB`;
}

function fmtPercent(used, total) {
  if (!total) return "0%";
  return `${Math.round((used / total) * 100)}%`;
}

const SOUND_SLOT_META = {
  startup:    { labelKey: "soundSlots.startup",   event: "system.startup",   icon: "ready",      tone: "ok"     },
  power_off:  { labelKey: "soundSlots.power_off", event: "system.power_off", icon: "power",      tone: "coral"  },
  nfc_mode:   { labelKey: "soundSlots.nfc_mode",  event: "mode.nfc",         icon: "tag",        tone: "coral"  },
  music_mode: { labelKey: "soundSlots.music_mode",event: "mode.music",       icon: "music",      tone: "petrol" },
};

function getSoundSlotMeta(name, t) {
  const meta = SOUND_SLOT_META[name] || { labelKey: null, event: name, icon: "sound", tone: "petrol" };
  return {
    ...meta,
    label: meta.labelKey ? t(meta.labelKey) : name,
  };
}

function waveBars(n, seed = 7) {
  const out = [];
  let s = seed;
  for (let i = 0; i < n; i++) {
    s = (s * 9301 + 49297) % 233280;
    const r = s / 233280;
    const env = 0.4 + 0.6 * Math.abs(Math.sin((i / n) * Math.PI * 1.4 + 0.6));
    const noise = 0.5 + 0.5 * r;
    out.push(Math.max(0.12, env * noise));
  }
  return out;
}

function buildFileTree(flatFiles, locale = DEFAULT_LOCALE) {
  const t = createTranslator(locale);
  const root = [];
  const folders = {};

  (flatFiles || []).forEach(f => {
    const parts = f.path.split("/");
    if (parts.length === 1) {
      const ext = parts[0].split(".").pop().toLowerCase();
      root.push({
        name: parts[0],
        type: ext === "mp3" ? "audio" : ext === "json" ? "config" : ext === "txt" ? "log" : "file",
        size: fmtBytes(f.size),
        count: "—",
        mtime: f.mtime,
      });
    } else {
      const folderName = parts[0] + "/";
      if (!folders[folderName]) {
        folders[folderName] = { name: folderName, type: "folder", children: [], totalSize: 0, count: 0 };
        root.unshift(folders[folderName]);
      }
      const ext = parts[parts.length - 1].split(".").pop().toLowerCase();
      folders[folderName].children.push({
        name: parts.slice(1).join("/"),
        type: ext === "mp3" ? "audio" : "file",
        size: fmtBytes(f.size),
        count: "—",
        mtime: f.mtime,
      });
      folders[folderName].totalSize += f.size || 0;
      folders[folderName].count += 1;
    }
  });

  Object.values(folders).forEach(folder => {
    folder.size = fmtBytes(folder.totalSize);
    folder.count = t("device.filesWord", { count: folder.count });
  });

  return root;
}

Object.assign(window, {
  API_BASE,
  AppIntlCtx,
  TRANSLATIONS,
  apiFetch,
  apiPost,
  apiDelete,
  fmtDate,
  fmtDateTime,
  fmtBytes,
  fmtPercent,
  SOUND_SLOT_META,
  getSoundSlotMeta,
  waveBars,
  buildFileTree,
  useI18n,
  getStoredLocale,
  createTranslator,
  getLocaleTag,
});
