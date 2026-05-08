// Songs section — list, upload, YouTube import, trim, delete
const { useState: useStateS, useEffect: useEffectS, useContext: useContextS, useRef: useRefS } = React;

function SongsSection({ playing, setPlaying }) {
  const { tracks, refresh } = useContextS(AppCtx);
  const { locale, t } = useI18n();
  const [filter, setFilter] = useStateS("all");
  const [trimTrack, setTrimTrack] = useStateS(null);
  const [ytTask, setYtTask] = useStateS(null);
  const [ytUrl, setYtUrl] = useStateS("");
  const [ytTitle, setYtTitle] = useStateS("");
  const [ytError, setYtError] = useStateS(null);
  const [uploadError, setUploadError] = useStateS(null);
  const [uploading, setUploading] = useStateS(false);
  const fileInputRef = useRefS(null);
  const dropRef = useRefS(null);

  useEffectS(() => {
    if (!ytTask || ytTask.status === "completed" || ytTask.status === "error") return;
    const interval = setInterval(async () => {
      try {
        const status = await apiFetch(`/admin/tracks/youtube/${ytTask.taskId}/status`);
        setYtTask(prev => ({ ...prev, ...status, taskId: prev.taskId }));
        if (status.status === "completed") {
          refresh();
          clearInterval(interval);
        }
      } catch {
        clearInterval(interval);
      }
    }, 1500);
    return () => clearInterval(interval);
  }, [ytTask?.taskId, ytTask?.status]);

  const filtered = tracks.filter(track => filter === "all" || (filter === "trimmed" && track.filename.includes("_trimmed")));

  const handleUploadFile = async (file) => {
    if (!file) return;
    if (!file.name.toLowerCase().endsWith(".mp3")) {
      setUploadError(t("songs.onlyMp3"));
      return;
    }
    const title = prompt(t("songs.promptTrackTitle"), file.name.replace(/\.mp3$/i, "").replace(/_/g, " "));
    if (!title) return;
    setUploading(true);
    setUploadError(null);
    try {
      const fd = new FormData();
      fd.append("title", title);
      fd.append("file", file);
      const res = await fetch("/admin/tracks", { method: "POST", body: fd });
      if (!res.ok) throw new Error(await res.text());
      await refresh();
    } catch (e) {
      setUploadError(e.message);
    } finally {
      setUploading(false);
    }
  };

  const handleDrop = (e) => {
    e.preventDefault();
    const file = e.dataTransfer.files[0];
    if (file) handleUploadFile(file);
  };

  const handleYouTubeImport = async () => {
    if (!ytUrl.trim() || !ytTitle.trim()) return;
    setYtError(null);
    try {
      const fd = new FormData();
      fd.append("title", ytTitle);
      fd.append("youtube_url", ytUrl);
      const res = await fetch("/admin/tracks/youtube", { method: "POST", body: fd });
      if (!res.ok) throw new Error(await res.text());
      const data = await res.json();
      setYtTask({ taskId: data.task_id, title: ytTitle, progress: 0, status: "downloading" });
      setYtUrl("");
      setYtTitle("");
    } catch (e) {
      setYtError(e.message);
    }
  };

  const handleDelete = async (track) => {
    if (!confirm(t("songs.confirmDelete", { title: track.title }))) return;
    await apiFetch(`/admin/tracks/${track.id}`, { method: "DELETE" });
    if (playing === track.id) setPlaying(null);
    refresh();
  };

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>{t("songs.heading")}</h2>
          <p>{t("songs.description")}</p>
        </div>
        <div className="actions">
          <button className="btn btn-secondary" onClick={() => fileInputRef.current?.click()}>
            <I.upload/> {t("common.uploadTrack")}
          </button>
          <input ref={fileInputRef} type="file" accept=".mp3" style={{ display: "none" }} onChange={e => handleUploadFile(e.target.files[0])}/>
        </div>
      </div>

      {uploadError && <div className="notice err" style={{ marginBottom: 14 }}><I.alert/> {uploadError}</div>}

      <div className="cols-2">
        <div>
          <div className="toolbar">
            <div className="tabs">
              <button className={"tab " + (filter === "all" ? "active" : "")} onClick={() => setFilter("all")}>
                {t("songs.all")} · {tracks.length}
              </button>
              <button className={"tab " + (filter === "trimmed" ? "active" : "")} onClick={() => setFilter("trimmed")}>
                {t("songs.trimmed")} · {tracks.filter(track => track.filename.includes("_trimmed")).length}
              </button>
            </div>
          </div>

          <div className="card">
            <div className="row songs-head">
              <div></div>
              <div>{t("common.title")}</div>
              <div>{t("common.added")}</div>
              <div>{t("common.file")}</div>
              <div style={{ textAlign: "right" }}>{t("common.actions")}</div>
            </div>
            <div className="row-list">
              {filtered.length === 0 ? (
                <div style={{ padding: "32px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                  {tracks.length === 0 ? t("songs.emptyLibrary") : t("songs.noResults")}
                </div>
              ) : filtered.map(track => (
                <div key={track.id} className={"row songs-row " + (playing === track.id ? "is-playing" : "")}>
                  <button className="play-dot" onClick={() => setPlaying(playing === track.id ? null : track.id)} title={t("common.play")}>
                    {playing === track.id ? <I.pause size={14}/> : <I.play size={14}/>}
                  </button>
                  <div className="cell-title">
                    <div className="t">
                      {track.title}
                      {track.filename.includes("_trimmed") && <span className="chip ghost" style={{ marginLeft: 8, fontSize: 10 }}>{t("dashboard.trimmed")}</span>}
                    </div>
                    <div className="s">{track.filename}</div>
                  </div>
                  <div className="cell-num" style={{ fontSize: "var(--fs-caption)", color: "var(--ink-3)" }}>{fmtDate(track.created_at, locale)}</div>
                  <div className="cell-mono">{track.filename.split(".").pop().toUpperCase()}</div>
                  <div className="row-actions">
                    <button className="btn-icon" title={t("songs.trimAction")} onClick={() => setTrimTrack(track)}><I.scissors size={15}/></button>
                    <button className="btn-icon btn-danger" title={t("songs.deleteAction")} onClick={() => handleDelete(track)}><I.trash size={15}/></button>
                  </div>
                </div>
              ))}
            </div>
          </div>

          <div style={{ marginTop: 14 }}>
            <div ref={dropRef} className="dropzone" onDragOver={e => e.preventDefault()} onDrop={handleDrop} onClick={() => fileInputRef.current?.click()}>
              <div className="ic">{uploading ? <I.sync size={20}/> : <I.upload size={20}/>}</div>
              <strong>{uploading ? t("songs.dropTitleUploading") : t("songs.dropTitle")}</strong>
              <div style={{ fontSize: 12 }}>{t("songs.dropHint")}</div>
            </div>
          </div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 18 }}>
          <YouTubeImportCard
            ytUrl={ytUrl} setYtUrl={setYtUrl}
            ytTitle={ytTitle} setYtTitle={setYtTitle}
            ytTask={ytTask} ytError={ytError}
            onImport={handleYouTubeImport}
            onDismissTask={() => setYtTask(null)}
          />
          {trimTrack
            ? <TrimEditorCard track={trimTrack} onClose={() => setTrimTrack(null)} onSaved={() => { refresh(); setTrimTrack(null); }}/>
            : <TrimPlaceholderCard/>
          }
        </div>
      </div>
    </div>
  );
}

function YouTubeImportCard({ ytUrl, setYtUrl, ytTitle, setYtTitle, ytTask, ytError, onImport, onDismissTask }) {
  const { t } = useI18n();
  const isRunning = ytTask && ytTask.status !== "completed" && ytTask.status !== "error";
  const isDone = ytTask?.status === "completed";
  const isFail = ytTask?.status === "error";

  return (
    <div className="card">
      <div className="card-head">
        <h3>{t("songs.youtubeImport")}</h3>
        {isRunning && <span className="chip warn">{t("songs.inProgress")}</span>}
        {isDone && <span className="chip ok">{t("songs.done")}</span>}
        {isFail && <span className="chip err">{t("songs.error")}</span>}
      </div>
      <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
        <div className="field">
          <label>{t("songs.youtubeLink")}</label>
          <input value={ytUrl} onChange={e => setYtUrl(e.target.value)} placeholder="https://www.youtube.com/watch?v=..." disabled={isRunning}/>
        </div>
        <div className="field">
          <label>{t("songs.trackTitle")}</label>
          <input value={ytTitle} onChange={e => setYtTitle(e.target.value)} placeholder={t("songs.exampleTitle")} disabled={isRunning}/>
        </div>
        {ytError && <div className="notice err"><I.alert/> {ytError}</div>}
        {ytTask && (
          <div style={{ background: "var(--bg-2)", border: "1px solid var(--line-soft)", borderRadius: "var(--radius-md)", padding: 12, display: "flex", gap: 12, alignItems: "center" }}>
            <div style={{ width: 48, height: 38, borderRadius: 8, background: "var(--coral-soft)", display: "grid", placeItems: "center", color: "var(--coral)", flexShrink: 0 }}>
              <I.yt size={20}/>
            </div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontWeight: 600, fontSize: 13, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{ytTask.title}</div>
              {isRunning && (
                <>
                  <div className="bar-track" style={{ marginTop: 6, height: 6 }}>
                    <div className="bar-fill" style={{ width: `${ytTask.progress || 0}%` }}/>
                  </div>
                  <div style={{ display: "flex", justifyContent: "space-between", fontSize: 11, color: "var(--ink-3)", marginTop: 4 }}>
                    <span className="mono">{ytTask.message || t("songs.inProgress")}</span>
                    <span className="mono">{ytTask.progress || 0}%</span>
                  </div>
                </>
              )}
              {isDone && <div style={{ fontSize: 12, color: "var(--ok)", marginTop: 4 }}>{t("songs.addedToLibrary")}</div>}
              {isFail && <div style={{ fontSize: 12, color: "var(--err)", marginTop: 4 }}>{ytTask.error || t("songs.importError")}</div>}
            </div>
            <button className="btn-icon" onClick={onDismissTask}><I.x size={15}/></button>
          </div>
        )}
        <div className="notice"><I.info/><div>{t("songs.importBackground")}</div></div>
        <button className="btn btn-accent" style={{ justifyContent: "center" }} onClick={onImport} disabled={isRunning || !ytUrl.trim() || !ytTitle.trim()}>
          <I.yt size={14}/> {t("songs.importButton")}
        </button>
      </div>
    </div>
  );
}

function TrimEditorCard({ track, onClose, onSaved }) {
  const { t } = useI18n();
  const bars = waveBars(64, track.id);
  const [lo, setLo] = useStateS(0.0);
  const [hi, setHi] = useStateS(1.0);
  const [saving, setSaving] = useStateS(false);
  const [error, setError] = useStateS(null);
  const [duration, setDuration] = useStateS(null);
  const [isPlaying, setIsPlaying] = useStateS(false);
  const [playPos, setPlayPos] = useStateS(null);
  const audioRef = useRefS(null);
  const hiRef = useRefS(1.0);

  useEffectS(() => { hiRef.current = hi; }, [hi]);

  useEffectS(() => {
    const audio = new Audio(`/api/stream/file/${track.filename}`);
    audioRef.current = audio;
    audio.addEventListener("loadedmetadata", () => setDuration(audio.duration));
    audio.addEventListener("timeupdate", () => {
      if (!audio.duration) return;
      const pos = audio.currentTime / audio.duration;
      setPlayPos(pos);
      if (audio.currentTime >= hiRef.current * audio.duration) {
        audio.pause();
        setIsPlaying(false);
        setPlayPos(null);
      }
    });
    audio.addEventListener("ended", () => { setIsPlaying(false); setPlayPos(null); });
    audio.load();
    return () => {
      audio.pause();
      audio.src = "";
      audioRef.current = null;
      setIsPlaying(false);
      setPlayPos(null);
    };
  }, [track.filename]);

  const togglePlay = () => {
    const audio = audioRef.current;
    if (!audio || !duration) return;
    if (isPlaying) {
      audio.pause();
      setIsPlaying(false);
      setPlayPos(null);
    } else {
      audio.currentTime = lo * duration;
      audio.play().catch(() => {});
      setIsPlaying(true);
    }
  };

  const handleSave = async () => {
    if (!duration) return;
    const startTime = lo * duration;
    const endTime = hi * duration;
    setSaving(true);
    setError(null);
    try {
      const fd = new FormData();
      fd.append("start_time", startTime.toFixed(2));
      fd.append("end_time", endTime.toFixed(2));
      const res = await fetch(`/admin/tracks/${track.id}/trim`, { method: "POST", body: fd });
      if (!res.ok) throw new Error(await res.text());
      onSaved();
    } catch (e) {
      setError(e.message);
    } finally {
      setSaving(false);
    }
  };

  const fmtSec = (pct) => {
    if (!duration) return "—";
    const secs = Math.floor(pct * duration);
    return `${Math.floor(secs / 60)}:${(secs % 60).toString().padStart(2, "0")}`;
  };

  return (
    <div className="card">
      <div className="card-head">
        <h3>{t("songs.trimming")}</h3>
        <div className="actions">
          <button className="btn-icon" onClick={onClose}><I.x size={15}/></button>
        </div>
      </div>
      <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 14 }}>
        <div className="trim-head">
          <div className="trim-art">{track.title[0].toUpperCase()}</div>
          <div className="trim-meta">
            <div className="t">{track.title}</div>
            <div className="s">{track.filename}</div>
          </div>
        </div>

        <div className="waveform" style={{ cursor: "crosshair", position: "relative" }}>
          <div className="bars">
            {bars.map((h, i) => {
              const ratio = i / bars.length;
              const inSel = ratio >= lo && ratio <= hi;
              return <div key={i} className={"bar " + (inSel ? "in" : "")} style={{ height: `${Math.round(h * 100)}%` }}/>;
            })}
          </div>
          <div className="selection" style={{ left: `${lo * 100}%`, right: `${(1 - hi) * 100}%` }}/>
          {playPos !== null && <div style={{ position: "absolute", top: 0, bottom: 0, left: `${playPos * 100}%`, width: 2, background: "var(--accent)", zIndex: 10, pointerEvents: "none", borderRadius: 1 }}/>}
        </div>

        <div style={{ display: "flex", justifyContent: "center" }}>
          <button className="btn btn-secondary" style={{ gap: 8, paddingLeft: 20, paddingRight: 20 }} onClick={togglePlay} disabled={!duration}>
            {isPlaying ? <I.pause size={15}/> : <I.play size={15}/>}
            {isPlaying ? t("songs.stop") : t("songs.previewSelection")}
          </button>
        </div>

        <div className="trim-times">
          <div className="trim-time">
            <div className="lbl">{t("songs.start")}</div>
            <div className="v">{fmtSec(lo)}</div>
          </div>
          <div className="trim-time range">
            <div className="lbl">{t("songs.length")}</div>
            <div className="v">{fmtSec(hi - lo)}</div>
          </div>
          <div className="trim-time">
            <div className="lbl">{t("songs.end")}</div>
            <div className="v">{fmtSec(hi)}</div>
          </div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          <div className="field">
            <label>{t("songs.startSeconds")}</label>
            <input type="range" min="0" max="1" step="0.01" value={lo} onChange={e => { const value = parseFloat(e.target.value); if (value < hi) setLo(value); }} style={{ width: "100%" }}/>
          </div>
          <div className="field">
            <label>{t("songs.endSeconds")}</label>
            <input type="range" min="0" max="1" step="0.01" value={hi} onChange={e => { const value = parseFloat(e.target.value); if (value > lo) setHi(value); }} style={{ width: "100%" }}/>
          </div>
        </div>

        {error && <div className="notice err"><I.alert/> {error}</div>}

        <div style={{ display: "flex", gap: 8 }}>
          <button className="btn btn-ghost" onClick={onClose}>{t("common.cancel")}</button>
          <div style={{ flex: 1 }}/>
          <button className="btn btn-primary" onClick={handleSave} disabled={saving || !duration}>
            {saving ? <I.sync size={14}/> : <I.check size={14}/>}
            {saving ? t("songs.saving") : t("songs.saveTrim")}
          </button>
        </div>
      </div>
    </div>
  );
}

function TrimPlaceholderCard() {
  const { t } = useI18n();
  return (
    <div className="card" style={{ borderStyle: "dashed", background: "transparent" }}>
      <div className="card-body" style={{ display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center", gap: 10, padding: "32px", textAlign: "center", color: "var(--ink-3)" }}>
        <I.scissors size={28}/>
        <div style={{ fontWeight: 600, fontSize: "var(--fs-small)", color: "var(--ink-2)" }}>{t("songs.trimEditor")}</div>
        <div style={{ fontSize: "var(--fs-caption)" }}>{t("songs.trimEditorHint")}</div>
      </div>
    </div>
  );
}

window.SongsSection = SongsSection;
