// System sounds section
const { useState: useStateSnd, useContext: useContextSnd, useRef: useRefSnd } = React;

function SoundsSection({ playing, setPlaying }) {
  const { sounds, refresh } = useContextSnd(AppCtx);

  const slotsWithMeta = sounds.map(s => ({
    ...s,
    ...(SOUND_SLOT_META[s.name] || { label: s.name, event: s.name, icon: "sound", tone: "petrol" }),
    set: !!s.filename,
  }));

  const setCount = slotsWithMeta.filter(s => s.set).length;

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>Dźwięki systemowe</h2>
          <p>Sygnały odtwarzane przez urządzenie podczas zdarzeń. Każdy slot to jedno konkretne zdarzenie.</p>
        </div>
        <div className="actions">
          <button className="btn btn-secondary" onClick={() => {
            if (!confirm("Usunąć wszystkie dźwięki systemowe?")) return;
            Promise.all(slotsWithMeta.filter(s => s.set).map(s => apiFetch(`/admin/system_sounds/${s.name}`, { method: "DELETE" })))
              .then(() => refresh());
          }}>
            <I.download size={14}/> Przywróć domyślne
          </button>
        </div>
      </div>

      <div style={{ marginBottom: 18 }}>
        <div className="notice">
          <I.info/>
          <div>
            <strong>{setCount} z {slotsWithMeta.length} slotów</strong> ma przypisany dźwięk.
            Puste sloty używają domyślnego sygnału wbudowanego w firmware.
          </div>
        </div>
      </div>

      <div className="slot-grid">
        {slotsWithMeta.map(s => <SoundSlot key={s.id} s={s} playing={playing} setPlaying={setPlaying} onRefresh={refresh}/>)}
      </div>
    </div>
  );
}

function SoundSlot({ s, playing, setPlaying, onRefresh }) {
  const Icon = I[s.icon] || I.sound;
  const fileInputRef = useRefSnd(null);
  const [uploading, setUploading] = useStateSnd(false);
  const [error, setError] = useStateSnd(null);

  const tones = {
    petrol: ["var(--petrol-soft)", "var(--petrol)"],
    coral:  ["var(--coral-soft)",  "var(--coral)"],
    sage:   ["var(--sage-soft)",   "var(--sage)"],
    amber:  ["var(--amber-soft)",  "#8B6F2A"],
    ok:     ["var(--ok-soft)",     "var(--ok)"],
  };
  const [bg, fg] = tones[s.tone] || tones.petrol;
  const bars = waveBars(40, s.name.charCodeAt(0));

  const handleUpload = async (file) => {
    if (!file || !file.name.toLowerCase().endsWith(".mp3")) {
      setError("Dozwolone tylko pliki MP3");
      return;
    }
    setUploading(true);
    setError(null);
    try {
      const fd = new FormData();
      fd.append("file", file);
      const res = await fetch(`/admin/system_sounds/${s.name}`, { method: "POST", body: fd });
      if (!res.ok) throw new Error(await res.text());
      onRefresh();
    } catch (e) {
      setError(e.message);
    } finally {
      setUploading(false);
    }
  };

  const handleDelete = async () => {
    if (!confirm(`Usunąć dźwięk "${s.label}"?`)) return;
    await apiFetch(`/admin/system_sounds/${s.name}`, { method: "DELETE" });
    if (playing === `sound:${s.name}`) setPlaying(null);
    onRefresh();
  };

  const handlePlay = () => {
    setPlaying(playing === `sound:${s.name}` ? null : `sound:${s.name}`);
    if (s.filename) {
      const audio = new Audio(`/api/system_sounds/${s.name}`);
      audio.play().catch(() => {});
    }
  };

  return (
    <div className="slot">
      <div className="slot-head">
        <div className="slot-icon" style={{ background: bg, color: fg }}><Icon size={18}/></div>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div className="slot-name">{s.label}</div>
          <div className="slot-event">{s.event}</div>
        </div>
        {s.set
          ? <span className="chip ok"><I.check size={11}/> przypisany</span>
          : <span className="chip ghost">pusty</span>}
      </div>

      {error && <div className="notice err" style={{ padding: "8px 10px", fontSize: 11 }}><I.alert size={14}/> {error}</div>}

      {s.set ? (
        <>
          <div className="slot-body">
            <button className="play-dot" style={{ flexShrink: 0 }} onClick={handlePlay} title="Odsłuchaj">
              {playing === `sound:${s.name}` ? <I.pause size={13}/> : <I.play size={13}/>}
            </button>
            <div className="slot-mini-wave">
              {bars.map((h, i) => (
                <div key={i} className={"bar " + (i < 14 ? "in" : "")} style={{ height: `${Math.round(h * 100)}%` }}/>
              ))}
            </div>
            <span className="slot-len">{s.filename || "—"}</span>
          </div>
          <div className="slot-foot">
            <button className="btn btn-secondary" style={{ flex: 1, justifyContent: "center" }} onClick={() => fileInputRef.current?.click()} disabled={uploading}>
              {uploading ? <I.sync size={13}/> : <I.upload size={13}/>} Podmień
            </button>
            <button className="btn btn-ghost btn-danger" onClick={handleDelete}><I.trash size={14}/></button>
          </div>
        </>
      ) : (
        <>
          <div className="slot-body empty" onClick={() => fileInputRef.current?.click()}>
            {uploading ? <I.sync size={16}/> : <I.upload size={16}/>}
            <span style={{ marginLeft: 8 }}>{uploading ? "Wgrywanie..." : "Upuść plik MP3 lub kliknij"}</span>
          </div>
          <div className="slot-foot">
            <button className="btn btn-secondary" style={{ flex: 1, justifyContent: "center" }} onClick={() => fileInputRef.current?.click()} disabled={uploading}>
              <I.upload size={13}/> Wgraj plik
            </button>
          </div>
        </>
      )}
      <input ref={fileInputRef} type="file" accept=".mp3" style={{ display: "none" }}
        onChange={e => handleUpload(e.target.files[0])}/>
    </div>
  );
}

window.SoundsSection = SoundsSection;
