// Tags (Figurines) section
const { useState: useStateT, useEffect: useEffectT, useContext: useContextT, useRef: useRefT } = React;

function TagsSection() {
  const { figurines, tracks, refresh } = useContextT(AppCtx);
  const { t } = useI18n();
  const [tabFilter, setTabFilter] = useStateT("all");
  const [assignFigurineId, setAssignFigurineId] = useStateT(null);
  const [assignTrackId, setAssignTrackId] = useStateT("");
  const [addName, setAddName] = useStateT("");
  const [addUid, setAddUid] = useStateT("");
  const [addError, setAddError] = useStateT(null);
  const [saving, setSaving] = useStateT(false);

  const assigned = figurines.filter(item => !!item.track_id);
  const empty = figurines.filter(item => !item.track_id);
  const filtered = figurines.filter(item => tabFilter === "assigned" ? !!item.track_id : tabFilter === "empty" ? !item.track_id : true);

  const handleDelete = async (figurine) => {
    if (!confirm(t("tags.confirmDelete", { name: figurine.name }))) return;
    await apiFetch(`/admin/figurines/${figurine.id}`, { method: "DELETE" });
    refresh();
  };

  const handleAssignSave = async () => {
    if (!assignFigurineId) return;
    setSaving(true);
    try {
      await apiFetch(`/admin/figurines/${assignFigurineId}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ track_id: assignTrackId ? parseInt(assignTrackId, 10) : 0 }),
      });
      setAssignFigurineId(null);
      setAssignTrackId("");
      refresh();
    } finally {
      setSaving(false);
    }
  };

  const handleAddFigurine = async () => {
    if (!addName.trim() || !addUid.trim()) return;
    setAddError(null);
    setSaving(true);
    try {
      await apiFetch("/admin/figurines", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name: addName.trim(), nfc_uid: addUid.trim() }),
      });
      setAddName("");
      setAddUid("");
      refresh();
    } catch (e) {
      setAddError(e.message);
    } finally {
      setSaving(false);
    }
  };

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>{t("tags.heading")}</h2>
          <p>{t("tags.description")}</p>
        </div>
      </div>

      <div className="cols-12">
        <div>
          <div className="toolbar">
            <div className="tabs">
              <button className={"tab " + (tabFilter === "all" ? "active" : "")} onClick={() => setTabFilter("all")}>{t("songs.all")} · {figurines.length}</button>
              <button className={"tab " + (tabFilter === "assigned" ? "active" : "")} onClick={() => setTabFilter("assigned")}>{t("tags.withTrack")} · {assigned.length}</button>
              <button className={"tab " + (tabFilter === "empty" ? "active" : "")} onClick={() => setTabFilter("empty")}>{t("tags.empty")} · {empty.length}</button>
            </div>
          </div>

          <div className="card">
            <div className="row tags-head">
              <div></div>
              <div>{t("tags.tagName")}</div>
              <div>{t("tags.assignedTrack")}</div>
              <div style={{ textAlign: "right" }}>{t("common.actions")}</div>
            </div>
            <div className="row-list">
              {filtered.length === 0 ? (
                <div style={{ padding: "32px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                  {figurines.length === 0 ? t("tags.noTags") : t("songs.noResults")}
                </div>
              ) : filtered.map(figurine => (
                <div key={figurine.id} className="row tags-row">
                  <div className="tag-thumb figurine"><I.figurine/></div>
                  <div className="cell-title">
                    <div className="t">{figurine.name}</div>
                    <div className="s" style={{ fontFamily: "var(--font-mono)", fontSize: 11 }}>{figurine.nfc_uid}</div>
                  </div>
                  <div>
                    {figurine.track
                      ? <div className="t" style={{ fontSize: 14, fontWeight: 500 }}>{figurine.track.title}</div>
                      : <span className="chip warn"><I.unlink size={11}/> {t("tags.noAssignment")}</span>}
                  </div>
                  <div className="row-actions">
                    <button className="btn-icon" title={t("tags.assignTrack")} onClick={() => { setAssignFigurineId(figurine.id); setAssignTrackId(figurine.track_id ? String(figurine.track_id) : ""); }}><I.link size={15}/></button>
                    <button className="btn-icon btn-danger" title={t("common.delete")} onClick={() => handleDelete(figurine)}><I.trash size={15}/></button>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 18 }}>
          <div className="mobile-only"><NfcScanCard onUidFound={uid => setAddUid(uid)}/></div>

          <div className="card">
            <div className="card-head"><h3>{t("tags.quickAssign")}</h3></div>
            <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
              <div className="field">
                <label>{t("tags.tagOrFigurine")}</label>
                <select value={assignFigurineId || ""} onChange={e => {
                  const id = e.target.value ? parseInt(e.target.value, 10) : null;
                  setAssignFigurineId(id);
                  const figurine = figurines.find(item => item.id === id);
                  setAssignTrackId(figurine?.track_id ? String(figurine.track_id) : "");
                }}>
                  <option value="">— {t("tags.chooseTag")} —</option>
                  {figurines.map(figurine => (
                    <option key={figurine.id} value={figurine.id}>{figurine.name}{!figurine.track_id ? ` · ${t("tags.noAssignment").toLowerCase()}` : ""}</option>
                  ))}
                </select>
              </div>
              <div style={{ display: "flex", alignItems: "center", justifyContent: "center", color: "var(--ink-4)" }}>
                <I.arrowRight size={16} style={{ transform: "rotate(90deg)" }}/>
              </div>
              <div className="field">
                <label>{t("common.track")}</label>
                <select value={assignTrackId} onChange={e => setAssignTrackId(e.target.value)}>
                  <option value="">— {t("tags.removeAssignment")} —</option>
                  {tracks.map(track => <option key={track.id} value={track.id}>{track.title}</option>)}
                </select>
              </div>
              <div className="notice"><I.info/><div>{t("tags.syncAfterSave")}</div></div>
              <button className="btn btn-accent" style={{ justifyContent: "center" }} onClick={handleAssignSave} disabled={saving || !assignFigurineId}>
                {saving ? <I.sync size={14}/> : <I.check size={14}/>} {t("tags.assignTrack")}
              </button>
            </div>
          </div>

          <div className="card">
            <div className="card-head"><h3>{t("tags.addManually")}</h3></div>
            <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
              <div className="field">
                <label>{t("tags.name")}</label>
                <input value={addName} onChange={e => setAddName(e.target.value)} placeholder="Smok Iggy"/>
              </div>
              <div className="field">
                <label>UID (hex)</label>
                <input value={addUid} onChange={e => setAddUid(e.target.value)} placeholder="04:A2:19:7E:3F:80:02" style={{ fontFamily: "var(--font-mono)" }}/>
              </div>
              {addError && <div className="notice err"><I.alert/> {addError}</div>}
              <button className="btn btn-secondary" style={{ justifyContent: "center" }} onClick={handleAddFigurine} disabled={saving || !addName.trim() || !addUid.trim()}>
                <I.plus size={14}/> {t("tags.addTag")}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

function NfcScanCard({ onUidFound }) {
  const { figurines } = useContextT(AppCtx);
  const { t } = useI18n();
  const nfcAvailable = typeof NDEFReader !== "undefined";
  const secureCtx = window.isSecureContext;
  const [scanState, setScanState] = useStateT("idle");
  const [scannedUid, setScannedUid] = useStateT(null);
  const [foundFig, setFoundFig] = useStateT(null);
  const [errMsg, setErrMsg] = useStateT(null);
  const abortRef = useRefT(null);

  useEffectT(() => () => abortRef.current?.abort(), []);

  const startScan = async () => {
    setErrMsg(null);
    setScannedUid(null);
    setFoundFig(null);
    setScanState("scanning");
    const ctrl = new AbortController();
    abortRef.current = ctrl;
    try {
      const ndef = new NDEFReader();
      await ndef.scan({ signal: ctrl.signal });
      ndef.addEventListener("reading", ({ serialNumber }) => {
        ctrl.abort();
        const uid = serialNumber.toUpperCase();
        const existing = figurines.find(item => item.nfc_uid === uid);
        setScannedUid(uid);
        setFoundFig(existing || null);
        setScanState("found");
        if (!existing) onUidFound(uid);
      }, { signal: ctrl.signal });
    } catch (e) {
      if (e.name === "AbortError") { setScanState("idle"); return; }
      setErrMsg(e.message || t("tags.scanError"));
      setScanState("error");
    }
  };

  const stopScan = () => { abortRef.current?.abort(); };
  const reset = () => { setScannedUid(null); setFoundFig(null); setScanState("idle"); };

  if (!secureCtx) {
    return (
      <div className="card">
        <div className="card-head"><h3>{t("tags.scanNfc")}</h3><span className="chip warn">{t("tags.requiresHttps")}</span></div>
        <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <div className="notice warn">
            <I.alert/>
            <div>
              {t("tags.webNfcHttps")}<br/>
              <code style={{ display: "block", marginTop: 6, fontSize: 11 }}>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code>
            </div>
          </div>
        </div>
      </div>
    );
  }

  if (!nfcAvailable) {
    return (
      <div className="card">
        <div className="card-head"><h3>{t("tags.scanNfc")}</h3><span className="chip">{t("tags.androidOnly")}</span></div>
        <div className="card-body">
          <div className="notice"><I.info/><div>{t("tags.webNfcOnly")}</div></div>
        </div>
      </div>
    );
  }

  return (
    <div className="card">
      <div className="card-head">
        <h3>{t("tags.scanNfc")}</h3>
        {scanState === "scanning" && <span className="chip warn">{t("common.waiting")}</span>}
        {scanState === "found" && <span className="chip ok">{t("common.read")}</span>}
      </div>
      <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
        {scanState === "idle" && (
          <button className="btn btn-accent" style={{ justifyContent: "center" }} onClick={startScan}>
            <I.nfc size={16}/> {t("tags.scanTag")}
          </button>
        )}

        {scanState === "scanning" && (
          <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 16, padding: "16px 0" }}>
            <div className="nfc-pulse"><I.nfc size={32}/></div>
            <div style={{ textAlign: "center", color: "var(--ink-2)", fontSize: "var(--fs-small)", fontWeight: 500 }}>{t("tags.holdToPhone")}</div>
            <button className="btn btn-ghost" onClick={stopScan}>{t("common.cancel")}</button>
          </div>
        )}

        {scanState === "found" && scannedUid && (
          <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div style={{ background: "var(--bg-2)", border: "1px solid var(--line-soft)", borderRadius: "var(--radius-md)", padding: "10px 14px", fontFamily: "var(--font-mono)", fontSize: 12, wordBreak: "break-all", letterSpacing: "0.04em" }}>
              {scannedUid}
            </div>
            {foundFig ? (
              <div className="notice ok">
                <I.check/>
                <div>{t("tags.knownFigurine", { name: foundFig.name, track: foundFig.track?.title || "" })}</div>
              </div>
            ) : (
              <div className="notice"><I.info/><div>{t("tags.newTagHint")}</div></div>
            )}
            <button className="btn btn-ghost" style={{ justifyContent: "center" }} onClick={reset}>
              <I.nfc size={14}/> {t("tags.scanAgain")}
            </button>
          </div>
        )}

        {scanState === "error" && (
          <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div className="notice err"><I.alert/> {errMsg}</div>
            <button className="btn btn-ghost" style={{ justifyContent: "center" }} onClick={reset}>{t("tags.scanRetry")}</button>
          </div>
        )}
      </div>
    </div>
  );
}

window.TagsSection = TagsSection;
