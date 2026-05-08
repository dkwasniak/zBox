// System sounds section
const { useState: useStateSnd, useContext: useContextSnd, useRef: useRefSnd } = React;

function SoundsSection({ playing, setPlaying }) {
  const { sounds, refresh } = useContextSnd(AppCtx);
  const { t } = useI18n();

  const slotsWithMeta = sounds.map(sound => ({
    ...sound,
    ...getSoundSlotMeta(sound.name, t),
    set: !!sound.filename,
  }));

  const setCount = slotsWithMeta.filter(sound => sound.set).length;

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>{t("sounds.heading")}</h2>
          <p>{t("sounds.description")}</p>
        </div>
        <div className="actions">
          <button className="btn btn-secondary" onClick={() => {
            if (!confirm(t("sounds.resetConfirm"))) return;
            Promise.all(slotsWithMeta.filter(sound => sound.set).map(sound => apiFetch(`/admin/system_sounds/${sound.name}`, { method: "DELETE" })))
              .then(() => refresh());
          }}>
            <I.download size={14}/> {t("sounds.resetButton")}
          </button>
        </div>
      </div>

      <div style={{ marginBottom: 18 }}>
        <div className="notice">
          <I.info/>
          <div>{t("sounds.summary", { setCount, total: slotsWithMeta.length })}</div>
        </div>
      </div>

      <div className="slot-grid">
        {slotsWithMeta.map(sound => <SoundSlot key={sound.id} s={sound} playing={playing} setPlaying={setPlaying} onRefresh={refresh}/>)}
      </div>
    </div>
  );
}

function SoundSlot({ s, playing, setPlaying, onRefresh }) {
  const { t } = useI18n();
  const Icon = I[s.icon] || I.sound;
  const fileInputRef = useRefSnd(null);
  const [uploading, setUploading] = useStateSnd(false);
  const [error, setError] = useStateSnd(null);

  const tones = {
    petrol: ["var(--petrol-soft)", "var(--petrol)"],
    coral: ["var(--coral-soft)", "var(--coral)"],
    sage: ["var(--sage-soft)", "var(--sage)"],
    amber: ["var(--amber-soft)", "#8B6F2A"],
    ok: ["var(--ok-soft)", "var(--ok)"],
  };
  const [bg, fg] = tones[s.tone] || tones.petrol;
  const bars = waveBars(40, s.name.charCodeAt(0));

  const handleUpload = async (file) => {
    if (!file || !file.name.toLowerCase().endsWith(".mp3")) {
      setError(t("songs.onlyMp3"));
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
    if (!confirm(t("sounds.confirmDelete", { label: s.label }))) return;
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
          ? <span className="chip ok"><I.check size={11}/> {t("sounds.assigned")}</span>
          : <span className="chip ghost">{t("sounds.empty")}</span>}
      </div>

      {error && <div className="notice err" style={{ padding: "8px 10px", fontSize: 11 }}><I.alert size={14}/> {error}</div>}

      {s.set ? (
        <>
          <div className="slot-body">
            <button className="play-dot" style={{ flexShrink: 0 }} onClick={handlePlay} title={t("sounds.preview")}>
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
              {uploading ? <I.sync size={13}/> : <I.upload size={13}/>} {t("sounds.replace")}
            </button>
            <button className="btn btn-ghost btn-danger" onClick={handleDelete}><I.trash size={14}/></button>
          </div>
        </>
      ) : (
        <>
          <div className="slot-body empty" onClick={() => fileInputRef.current?.click()}>
            {uploading ? <I.sync size={16}/> : <I.upload size={16}/>}
            <span style={{ marginLeft: 8 }}>{uploading ? t("sounds.uploading") : t("sounds.clickOrDrop")}</span>
          </div>
          <div className="slot-foot">
            <button className="btn btn-secondary" style={{ flex: 1, justifyContent: "center" }} onClick={() => fileInputRef.current?.click()} disabled={uploading}>
              <I.upload size={13}/> {t("common.upload")}
            </button>
          </div>
        </>
      )}
      <input ref={fileInputRef} type="file" accept=".mp3" style={{ display: "none" }} onChange={e => handleUpload(e.target.files[0])}/>
    </div>
  );
}

window.SoundsSection = SoundsSection;
