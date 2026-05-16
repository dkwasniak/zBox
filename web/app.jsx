// Main app shell — state management + routing
const { useState, useEffect, useContext, createContext, useRef, useCallback } = React;

const AppCtx = createContext({
  tracks: [], figurines: [], sounds: [], device: null,
  deviceSettings: { ip: "" }, loading: true, error: null,
  refresh: () => {}, refreshDevice: () => {},
  syncActive: false, setSyncActive: () => {},
});

const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "palette": "mist",
  "density": "comfortable",
  "softness": "medium",
  "showLed": false
}/*EDITMODE-END*/;

function loadStoredDevice() {
  try {
    const raw = localStorage.getItem("zbox_last_device");
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    return parsed && parsed.device_id ? parsed : null;
  } catch {
    return null;
  }
}

function App() {
  const [tweaks, setTweak] = useTweaks(TWEAK_DEFAULTS);
  const [darkMode, setDarkMode] = useState(() => localStorage.getItem("zbox_dark") === "1");
  const [locale, setLocale] = useState(() => getStoredLocale());
  const [section, setSection] = useState("dashboard");
  const [playing, setPlaying] = useState(null);
  const t = createTranslator(locale);

  const [tracks, setTracks] = useState([]);
  const [figurines, setFigurines] = useState([]);
  const [sounds, setSounds] = useState([]);
  const [device, setDevice] = useState(() => loadStoredDevice());
  const [deviceSettings, setDeviceSettings] = useState({ ip: "" });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [syncActive, setSyncActive] = useState(false);
  const deviceFailureCountRef = useRef(0);

  const fetchAll = useCallback(async () => {
    try {
      const [tData, fData, sData, dsData] = await Promise.all([
        apiFetch("/admin/tracks"),
        apiFetch("/admin/figurines"),
        apiFetch("/admin/system_sounds"),
        apiFetch("/admin/device-settings").catch(() => ({ ip: "" })),
      ]);
      setTracks(tData || []);
      setFigurines(fData || []);
      setSounds(sData || []);
      setDeviceSettings(dsData || { ip: "" });
      setError(null);
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const fetchDevice = useCallback(async () => {
    if (syncActive) {
      return;
    }
    try {
      const devs = await apiFetch("/admin/devices");
      if (devs && devs.length > 0) {
        deviceFailureCountRef.current = 0;
        localStorage.setItem("zbox_last_device", JSON.stringify(devs[0]));
        setDevice(devs[0]);
        return;
      }

      deviceFailureCountRef.current += 1;
      setDevice(current => {
        const next = current && deviceFailureCountRef.current < 3 ? current : null;
        if (!next) localStorage.removeItem("zbox_last_device");
        return next;
      });
    } catch {
      deviceFailureCountRef.current += 1;
      setDevice(current => {
        const next = current && deviceFailureCountRef.current < 3 ? current : null;
        if (!next) localStorage.removeItem("zbox_last_device");
        return next;
      });
    }
  }, [syncActive]);

  useEffect(() => {
    fetchAll();
    fetchDevice();
    const intervalId = setInterval(fetchDevice, 30000);
    return () => clearInterval(intervalId);
  }, [fetchAll, fetchDevice]);

  useEffect(() => {
    document.documentElement.dataset.theme = darkMode ? "dark" : "";
    localStorage.setItem("zbox_dark", darkMode ? "1" : "0");
    if (darkMode) {
      const root = document.documentElement;
      root.style.removeProperty("--bg");
      root.style.removeProperty("--bg-2");
      root.style.removeProperty("--line");
      root.style.removeProperty("--line-soft");
    }
  }, [darkMode]);

  useEffect(() => {
    document.documentElement.lang = locale;
    localStorage.setItem("zbox_locale", locale);
  }, [locale]);

  useEffect(() => {
    document.title = `${sectionMetaFor(section, device, t).title} · zBox Admin`;
  }, [section, device?.hostname, locale]);

  useEffect(() => {
    if (darkMode) return;
    const root = document.documentElement;
    const palettes = {
      mist:  { bg: "#EEF1F6", bg2: "#F6F8FB", line: "#D2D9E3", lineSoft: "#DFE4ED" },
      ice:   { bg: "#E9EFF6", bg2: "#F3F7FB", line: "#CCD6E2", lineSoft: "#DCE3EC" },
      cream: { bg: "#F5F1E8", bg2: "#FBF7EE", line: "#DBD2BE", lineSoft: "#E8DFC9" },
      paper: { bg: "#F2F2F0", bg2: "#FAFAF8", line: "#D8D8D2", lineSoft: "#E2E2DC" },
    };
    const palette = palettes[tweaks.palette] || palettes.mist;
    root.style.setProperty("--bg", palette.bg);
    root.style.setProperty("--bg-2", palette.bg2);
    root.style.setProperty("--line", palette.line);
    root.style.setProperty("--line-soft", palette.lineSoft);
  }, [tweaks.palette, darkMode]);

  const cls = [
    tweaks.density === "dense" ? "dense" : "",
    tweaks.softness === "soft" ? "soft" : tweaks.softness === "crisp" ? "crisp" : "",
  ].filter(Boolean).join(" ");

  const meta = sectionMetaFor(section, device, t);
  const offline = !device;

  const ctx = {
    tracks, figurines, sounds, device, deviceSettings,
    loading, error,
    refresh: fetchAll,
    refreshDevice: fetchDevice,
    setDeviceSettings,
    syncActive,
    setSyncActive,
  };
  const intl = { locale, setLocale, t };

  return (
    <AppIntlCtx.Provider value={intl}>
      <AppCtx.Provider value={ctx}>
        <div className={"app " + cls}>
          <Sidebar section={section} setSection={setSection} showLed={tweaks.showLed} device={device} offline={offline}/>
          <div className="main">
            <Topbar
              meta={meta}
              darkMode={darkMode}
              toggleDark={() => setDarkMode(value => !value)}
              locale={locale}
              setLocale={setLocale}
            />
            <div className="content" data-screen-label={section}>
              {section === "dashboard" && <DashboardSection goto={setSection} offline={offline}/>}
              {section === "songs" && <SongsSection playing={playing} setPlaying={setPlaying}/>}
              {section === "tags" && <TagsSection playing={playing} setPlaying={setPlaying}/>}
              {section === "device" && <DeviceSection offline={offline}/>}
              {section === "sounds" && <SoundsSection playing={playing} setPlaying={setPlaying}/>}
            </div>
          </div>

          {playing !== null && <NowPlayingBar trackId={playing} tracks={tracks} onStop={() => setPlaying(null)}/>}
          <MobileNav section={section} setSection={setSection}/>

          <TweaksPanel title="Tweaks">
            <TweakSection title={t("common.look")}>
              <TweakRadio label={t("common.palette")} value={tweaks.palette} onChange={v => setTweak("palette", v)}
                options={[
                  { value: "mist", label: "Mist" },
                  { value: "ice", label: "Ice" },
                  { value: "cream", label: "Cream" },
                  { value: "paper", label: "Paper" },
                ]}/>
              <TweakRadio label={t("common.softness")} value={tweaks.softness} onChange={v => setTweak("softness", v)}
                options={[
                  { value: "crisp", label: "Crisp" },
                  { value: "medium", label: "Medium" },
                  { value: "soft", label: "Soft" },
                ]}/>
              <TweakRadio label={t("common.density")} value={tweaks.density} onChange={v => setTweak("density", v)}
                options={[
                  { value: "comfortable", label: t("common.comfortable") },
                  { value: "dense", label: t("common.dense") },
                ]}/>
            </TweakSection>
            <TweakSection title={t("common.sections")}>
              <TweakToggle label={t("common.showLed")} value={tweaks.showLed} onChange={v => setTweak("showLed", v)}/>
            </TweakSection>
          </TweaksPanel>
        </div>
      </AppCtx.Provider>
    </AppIntlCtx.Provider>
  );
}

function sectionMetaFor(section, device, t) {
  return {
    dashboard: { title: t("common.dashboard"), crumbs: "zBox" },
    songs: { title: t("common.songs"), crumbs: `zBox · ${t("common.library")}` },
    tags: { title: t("common.tags"), crumbs: `zBox · ${t("common.tags")}` },
    device: { title: t("common.device"), crumbs: `zBox · ${device?.hostname || "zBox"}` },
    sounds: { title: t("common.sounds"), crumbs: `zBox · ${t("common.signals")}` },
  }[section];
}

function MobileNav({ section, setSection }) {
  const { t } = useI18n();
  const items = [
    { id: "dashboard", label: t("common.dashboard"), icon: "grid" },
    { id: "songs", label: t("common.songs"), icon: "music" },
    { id: "tags", label: "Tags", icon: "tag" },
    { id: "device", label: t("common.device"), icon: "device" },
    { id: "sounds", label: t("common.sounds"), icon: "sound" },
  ];
  return (
    <nav className="mobile-nav">
      {items.map(it => {
        const Icon = I[it.icon];
        return (
          <button key={it.id} className={"mobile-nav-item " + (section === it.id ? "active" : "")} onClick={() => setSection(it.id)}>
            <Icon size={22}/>
            <span>{it.label}</span>
          </button>
        );
      })}
    </nav>
  );
}

function Sidebar({ section, setSection, showLed, device, offline }) {
  const { tracks, figurines, sounds } = useContext(AppCtx);
  const { t } = useI18n();

  const items = [{ id: "dashboard", label: t("common.dashboard"), icon: "grid" }];
  const library = [
    { id: "songs", label: t("common.songs"), icon: "music", count: tracks.length },
    { id: "tags", label: t("common.tags"), icon: "tag", count: figurines.length },
    { id: "sounds", label: t("common.sounds"), icon: "sound", count: `${sounds.filter(s => !!s.filename).length}/${sounds.length}` },
  ];
  const deviceItems = [{ id: "device", label: t("common.device"), icon: "device" }];

  return (
    <aside className="sidebar">
      <div className="brand">
        <ZboxLogo/>
        <div>
          <div className="brand-name"><span className="accent">z</span>Box</div>
          <div className="brand-sub">{t("app.brandSub")}</div>
        </div>
      </div>

      <nav className="nav">
        {items.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}
        <div className="nav-section">{t("common.library")}</div>
        {library.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}
        <div className="nav-section">{t("common.device")}</div>
        {deviceItems.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}

        {showLed && (
          <>
            <div className="nav-section">{t("app.soon")}</div>
            <div className="nav-item" style={{ opacity: 0.55, cursor: "default" }}>
              <I.spark/> <span>LED animations</span>
              <span className="count">{t("app.soon").toLowerCase()}</span>
            </div>
          </>
        )}
      </nav>

      <div className="sidebar-foot">
        <div className="device-pill">
          <div className="row">
            <span className={"dot " + (offline ? "" : "live")} style={offline ? { background: "var(--ink-4)" } : {}}/>
            <span className="name">{device ? `zBox · ${device.hostname}` : t("app.footerDeviceName")}</span>
          </div>
          <div className="ip">{offline ? t("app.noConnection") : device?.ip}</div>
          {offline ? (
            <div className="stat-row"><span>{t("common.offline").toLowerCase()}</span></div>
          ) : (
            <div className="stat-row">
              <span><I.battery size={10}/> {device?.battery_bars != null ? `${device.battery_bars}/5` : "?"}</span>
              <span><I.wifi size={10}/> {device?.rssi != null ? `${device.rssi} dBm` : "?"}</span>
              <span>{device?.mode || "NFC"}</span>
            </div>
          )}
        </div>
      </div>
    </aside>
  );
}

function NavItem({ it, active, onClick }) {
  const Icon = I[it.icon];
  return (
    <button className={"nav-item " + (active ? "active" : "")} onClick={onClick}>
      <Icon/>
      <span>{it.label}</span>
      {it.count !== undefined && <span className="count">{it.count}</span>}
    </button>
  );
}

function ZboxLogo() {
  const cols = 12, rows = 8;
  const dots = [];
  const rainbow = ["#1E5BA8", "#3D7CC9", "#6BB6A4", "#E8B547", "#E26B5A", "#C44E7C"];
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const onZ =
        (r === 1 && c >= 2 && c <= 9) ||
        (r === 6 && c >= 2 && c <= 9) ||
        (r === 2 && c === 8) ||
        (r === 3 && c === 7) ||
        (r === 3 && c === 6) ||
        (r === 4 && c === 5) ||
        (r === 4 && c === 4) ||
        (r === 5 && c === 3);
      const fill = onZ ? rainbow[(c + r) % rainbow.length] : "#1E5BA8";
      const op = onZ ? 1 : 0.22;
      dots.push({ r, c, fill, op });
    }
  }
  return (
    <svg className="brand-logo" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg" aria-label="zBox">
      <defs>
        <linearGradient id="zbox-body" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#2E6BBE"/>
          <stop offset="100%" stopColor="#143E78"/>
        </linearGradient>
        <clipPath id="zbox-face">
          <rect x="7" y="11" width="34" height="26" rx="5"/>
        </clipPath>
      </defs>
      <rect x="20" y="2" width="8" height="5" rx="1.6" fill="#143E78"/>
      <rect x="3" y="6" width="42" height="36" rx="9" fill="url(#zbox-body)"/>
      <rect x="7" y="11" width="34" height="26" rx="5" fill="#EEF1F6"/>
      <g clipPath="url(#zbox-face)">
        {dots.map((d, i) => (
          <circle key={i} cx={9.2 + d.c * 2.55} cy={13.2 + d.r * 2.95} r={1} fill={d.fill} opacity={d.op}/>
        ))}
      </g>
      <path d="M10 9 H38" stroke="#FFF" strokeOpacity="0.18" strokeWidth="1" strokeLinecap="round"/>
    </svg>
  );
}

function Topbar({ meta, darkMode, toggleDark, locale, setLocale }) {
  const { t } = useI18n();
  return (
    <div className="topbar">
      <div>
        <div className="crumbs">{meta.crumbs}</div>
        <h1>{meta.title}</h1>
      </div>
      <div className="spacer"/>
      <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
        <span style={{ fontSize: 12, color: "var(--ink-3)" }}>{t("app.language")}</span>
        <div style={{ display: "flex", border: "1px solid var(--line)", borderRadius: 999, overflow: "hidden" }}>
          {["en", "pl"].map(code => (
            <button
              key={code}
              className="btn-icon"
              onClick={() => setLocale(code)}
              title={code.toUpperCase()}
              style={{
                borderRadius: 0,
                width: 40,
                background: locale === code ? "var(--surface)" : "transparent",
                color: locale === code ? "var(--ink)" : "var(--ink-3)",
              }}
            >
              {code.toUpperCase()}
            </button>
          ))}
        </div>
      </div>
      <button className="btn-icon" onClick={toggleDark} title={darkMode ? t("common.lightMode") : t("common.darkMode")}>
        {darkMode ? <I.sun size={16}/> : <I.moon size={16}/>}
      </button>
    </div>
  );
}

function NowPlayingBar({ trackId, tracks, onStop }) {
  const { t } = useI18n();
  const track = tracks.find(item => item.id === trackId);
  const audioRef = useRef(null);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(0);
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    if (!track) return;
    const audio = new Audio(`/api/stream/file/${track.filename}`);
    audioRef.current = audio;
    audio.play().catch(() => {});
    audio.ontimeupdate = () => setCurrentTime(audio.currentTime);
    audio.onloadedmetadata = () => setDuration(audio.duration);
    audio.onended = onStop;
    audio.onplay = () => setPaused(false);
    audio.onpause = () => setPaused(true);
    return () => { audio.pause(); audio.src = ""; };
  }, [trackId]);

  if (!track) return null;

  const fmtTime = seconds => {
    if (!isFinite(seconds)) return "—";
    const mins = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    return `${mins}:${secs.toString().padStart(2, "0")}`;
  };

  const pct = duration > 0 ? (currentTime / duration) * 100 : 0;

  const togglePlay = () => {
    const audio = audioRef.current;
    if (!audio) return;
    paused ? audio.play() : audio.pause();
  };

  return (
    <div className="now-bar">
      <div className="np-art"><I.music size={18}/></div>
      <div className="np-meta">
        <div className="t">{track.title}</div>
        <div className="s">{fmtTime(duration)}</div>
      </div>
      <div className="np-progress">
        <span>{fmtTime(currentTime)}</span>
        <div className="track"><div className="fill" style={{ width: `${pct}%` }}/></div>
        <span>{fmtTime(duration)}</span>
      </div>
      <div className="np-controls">
        <button onClick={onStop} title={t("common.close")}><I.x size={14}/></button>
        <button className="play" onClick={togglePlay}>
          {paused ? <I.play size={14}/> : <I.pause size={14}/>}
        </button>
      </div>
    </div>
  );
}

window.AppCtx = AppCtx;

ReactDOM.createRoot(document.getElementById("root")).render(<App/>);
