// Main app shell — state management + routing
const { useState, useEffect, useContext, createContext, useRef, useCallback } = React;

// ── App Context ──────────────────────────────────────────────────────────────

const AppCtx = createContext({
  tracks: [], figurines: [], sounds: [], device: null,
  deviceSettings: { ip: "" }, loading: true, error: null,
  refresh: () => {}, refreshDevice: () => {},
});

// ── Tweak defaults ───────────────────────────────────────────────────────────

const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "palette": "mist",
  "density": "comfortable",
  "softness": "medium",
  "showLed": false
}/*EDITMODE-END*/;

// ── App root ─────────────────────────────────────────────────────────────────

function App() {
  const [tweaks, setTweak] = useTweaks(TWEAK_DEFAULTS);
  const [darkMode, setDarkMode] = useState(() => localStorage.getItem("zbox_dark") === "1");
  const [section, setSection] = useState("dashboard");
  const [playing, setPlaying] = useState(null);  // track id (string from API)

  // Data state
  const [tracks, setTracks] = useState([]);
  const [figurines, setFigurines] = useState([]);
  const [sounds, setSounds] = useState([]);
  const [device, setDevice] = useState(null);
  const [deviceSettings, setDeviceSettings] = useState({ ip: "" });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  const fetchAll = useCallback(async () => {
    try {
      const [t, f, s, ds] = await Promise.all([
        apiFetch("/admin/tracks"),
        apiFetch("/admin/figurines"),
        apiFetch("/admin/system_sounds"),
        apiFetch("/admin/device-settings").catch(() => ({ ip: "" })),
      ]);
      setTracks(t || []);
      setFigurines(f || []);
      setSounds(s || []);
      setDeviceSettings(ds || { ip: "" });
      setError(null);
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, []);

  const fetchDevice = useCallback(async () => {
    try {
      const devs = await apiFetch("/admin/devices");
      setDevice(devs && devs.length > 0 ? devs[0] : null);
    } catch {
      setDevice(null);
    }
  }, []);

  useEffect(() => {
    fetchAll();
    fetchDevice();
    // Refresh device status every 30s
    const t = setInterval(fetchDevice, 30000);
    return () => clearInterval(t);
  }, [fetchAll, fetchDevice]);

  // Dark mode
  useEffect(() => {
    document.documentElement.dataset.theme = darkMode ? "dark" : "";
    localStorage.setItem("zbox_dark", darkMode ? "1" : "0");
    // Clear any palette inline styles so CSS vars take over
    if (darkMode) {
      const root = document.documentElement;
      root.style.removeProperty("--bg");
      root.style.removeProperty("--bg-2");
      root.style.removeProperty("--line");
      root.style.removeProperty("--line-soft");
    }
  }, [darkMode]);

  // Palette swap (light mode only)
  useEffect(() => {
    if (darkMode) return;
    const root = document.documentElement;
    const palettes = {
      mist:  { bg: "#EEF1F6", bg2: "#F6F8FB", line: "#D2D9E3", lineSoft: "#DFE4ED" },
      ice:   { bg: "#E9EFF6", bg2: "#F3F7FB", line: "#CCD6E2", lineSoft: "#DCE3EC" },
      cream: { bg: "#F5F1E8", bg2: "#FBF7EE", line: "#DBD2BE", lineSoft: "#E8DFC9" },
      paper: { bg: "#F2F2F0", bg2: "#FAFAF8", line: "#D8D8D2", lineSoft: "#E2E2DC" },
    };
    const p = palettes[tweaks.palette] || palettes.mist;
    root.style.setProperty("--bg", p.bg);
    root.style.setProperty("--bg-2", p.bg2);
    root.style.setProperty("--line", p.line);
    root.style.setProperty("--line-soft", p.lineSoft);
  }, [tweaks.palette, darkMode]);

  const cls = [
    tweaks.density === "dense" ? "dense" : "",
    tweaks.softness === "soft" ? "soft" : tweaks.softness === "crisp" ? "crisp" : "",
  ].filter(Boolean).join(" ");

  const sectionMeta = {
    dashboard: { title: "Pulpit",               crumbs: "zBox" },
    songs:     { title: "Utwory",               crumbs: "zBox · Biblioteka" },
    tags:      { title: "Tagi NFC",             crumbs: "zBox · Tagi" },
    device:    { title: "Urządzenie",           crumbs: "zBox · " + (device?.hostname || "Lulu") },
    sounds:    { title: "Dźwięki systemowe",    crumbs: "zBox · Sygnały" },
  };
  const meta = sectionMeta[section];

  const offline = !device;

  const ctx = {
    tracks, figurines, sounds, device, deviceSettings,
    loading, error,
    refresh: fetchAll,
    refreshDevice: fetchDevice,
    setDeviceSettings,
  };

  return (
    <AppCtx.Provider value={ctx}>
      <div className={"app " + cls}>
        <Sidebar section={section} setSection={setSection} showLed={tweaks.showLed} device={device} offline={offline}/>
        <div className="main">
          <Topbar meta={meta} darkMode={darkMode} toggleDark={() => setDarkMode(d => !d)}/>
          <div className="content" data-screen-label={section}>
            {section === "dashboard" && <DashboardSection goto={setSection} offline={offline}/>}
            {section === "songs"     && <SongsSection playing={playing} setPlaying={setPlaying}/>}
            {section === "tags"      && <TagsSection playing={playing} setPlaying={setPlaying}/>}
            {section === "device"    && <DeviceSection offline={offline}/>}
            {section === "sounds"    && <SoundsSection playing={playing} setPlaying={setPlaying}/>}
          </div>
        </div>

        {playing !== null && <NowPlayingBar trackId={playing} tracks={tracks} onStop={() => setPlaying(null)}/>}

        <MobileNav section={section} setSection={setSection}/>

        <TweaksPanel title="Tweaks">
          <TweakSection title="Wygląd">
            <TweakRadio label="Paleta" value={tweaks.palette} onChange={v => setTweak("palette", v)}
              options={[
                { value: "mist",  label: "Mist" },
                { value: "ice",   label: "Ice" },
                { value: "cream", label: "Cream" },
                { value: "paper", label: "Paper" },
              ]}/>
            <TweakRadio label="Zaokrąglenia" value={tweaks.softness} onChange={v => setTweak("softness", v)}
              options={[
                { value: "crisp",  label: "Crisp" },
                { value: "medium", label: "Medium" },
                { value: "soft",   label: "Soft" },
              ]}/>
            <TweakRadio label="Gęstość" value={tweaks.density} onChange={v => setTweak("density", v)}
              options={[
                { value: "comfortable", label: "Wygodnie" },
                { value: "dense",       label: "Gęsto" },
              ]}/>
          </TweakSection>
          <TweakSection title="Sekcje">
            <TweakToggle label="Pokaż sekcję LED (wkrótce)" value={tweaks.showLed} onChange={v => setTweak("showLed", v)}/>
          </TweakSection>
        </TweaksPanel>
      </div>
    </AppCtx.Provider>
  );
}

// ── Mobile bottom nav ────────────────────────────────────────────────────────

function MobileNav({ section, setSection }) {
  const items = [
    { id: "dashboard", label: "Pulpit",  icon: "grid" },
    { id: "songs",     label: "Utwory",  icon: "music" },
    { id: "tags",      label: "Tagi",    icon: "tag" },
    { id: "device",    label: "Urządz.", icon: "device" },
    { id: "sounds",    label: "Dźwięki", icon: "sound" },
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

// ── Sidebar ──────────────────────────────────────────────────────────────────

function Sidebar({ section, setSection, showLed, device, offline }) {
  const { tracks, figurines, sounds } = useContext(AppCtx);

  const items = [{ id: "dashboard", label: "Pulpit", icon: "grid" }];
  const lib = [
    { id: "songs",  label: "Utwory",           icon: "music", count: tracks.length },
    { id: "tags",   label: "Tagi NFC",          icon: "tag",   count: figurines.length },
    { id: "sounds", label: "Dźwięki systemowe", icon: "sound", count: `${sounds.filter(s => !!s.filename).length}/${sounds.length}` },
  ];
  const dev = [{ id: "device", label: "Urządzenie", icon: "device" }];

  return (
    <aside className="sidebar">
      <div className="brand">
        <ZboxLogo/>
        <div>
          <div className="brand-name"><span className="accent">z</span>Box</div>
          <div className="brand-sub">family · audio</div>
        </div>
      </div>

      <nav className="nav">
        {items.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}

        <div className="nav-section">Biblioteka</div>
        {lib.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}

        <div className="nav-section">Urządzenie</div>
        {dev.map(it => <NavItem key={it.id} it={it} active={section === it.id} onClick={() => setSection(it.id)}/>)}

        {showLed && (
          <>
            <div className="nav-section">Wkrótce</div>
            <div className="nav-item" style={{ opacity: 0.55, cursor: "default" }}>
              <I.spark/> <span>Animacje LED</span>
              <span className="count">soon</span>
            </div>
          </>
        )}
      </nav>

      <div className="sidebar-foot">
        <div className="device-pill">
          <div className="row">
            <span className={"dot " + (offline ? "" : "live")} style={offline ? { background: "var(--ink-4)" } : {}}/>
            <span className="name">{device ? `zBox · ${device.hostname}` : "zBox"}</span>
          </div>
          <div className="ip">{offline ? "brak połączenia" : device?.ip}</div>
          {offline ? (
            <div className="stat-row"><span>offline</span></div>
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

// ── zBox Logo ────────────────────────────────────────────────────────────────

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

// ── Topbar ───────────────────────────────────────────────────────────────────

function Topbar({ meta, darkMode, toggleDark }) {
  return (
    <div className="topbar">
      <div>
        <div className="crumbs">{meta.crumbs}</div>
        <h1>{meta.title}</h1>
      </div>
      <div className="spacer"/>
      <button className="btn-icon" onClick={toggleDark} title={darkMode ? "Tryb jasny" : "Tryb ciemny"}>
        {darkMode ? <I.sun size={16}/> : <I.moon size={16}/>}
      </button>
    </div>
  );
}

// ── Now Playing Bar ──────────────────────────────────────────────────────────

function NowPlayingBar({ trackId, tracks, onStop }) {
  const track = tracks.find(t => t.id === trackId);
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

  const fmtTime = s => {
    if (!isFinite(s)) return "—";
    const m = Math.floor(s / 60);
    const sec = Math.floor(s % 60);
    return `${m}:${sec.toString().padStart(2, "0")}`;
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
        <button onClick={onStop} title="Zamknij"><I.x size={14}/></button>
        <button className="play" onClick={togglePlay}>
          {paused ? <I.play size={14}/> : <I.pause size={14}/>}
        </button>
      </div>
    </div>
  );
}

// Expose AppCtx to window so section files (separate script tags) can resolve it
window.AppCtx = AppCtx;

ReactDOM.createRoot(document.getElementById("root")).render(<App/>);
