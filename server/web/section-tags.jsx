// Tags (Figurines) section
const { useState: useStateT, useEffect: useEffectT, useContext: useContextT, useRef: useRefT } = React;

function TagsSection({ playing, setPlaying }) {
  const { figurines, tracks, refresh } = useContextT(AppCtx);
  const [tabFilter, setTabFilter] = useStateT("all");
  const [assignFigurineId, setAssignFigurineId] = useStateT(null);
  const [assignTrackId, setAssignTrackId] = useStateT("");
  const [addName, setAddName] = useStateT("");
  const [addUid, setAddUid] = useStateT("");
  const [addError, setAddError] = useStateT(null);
  const [saving, setSaving] = useStateT(false);

  const assigned = figurines.filter(f => !!f.track_id);
  const empty = figurines.filter(f => !f.track_id);

  const filtered = figurines.filter(f => {
    if (tabFilter === "assigned") return !!f.track_id;
    if (tabFilter === "empty") return !f.track_id;
    return true;
  });

  const handleDelete = async (fig) => {
    if (!confirm(`Usunąć tag "${fig.name}"?`)) return;
    await apiFetch(`/admin/figurines/${fig.id}`, { method: "DELETE" });
    refresh();
  };

  const handleAssignSave = async () => {
    if (!assignFigurineId) return;
    setSaving(true);
    try {
      await apiFetch(`/admin/figurines/${assignFigurineId}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ track_id: assignTrackId ? parseInt(assignTrackId) : 0 }),
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

  const openAssign = (fig) => {
    setAssignFigurineId(fig.id);
    setAssignTrackId(fig.track_id ? String(fig.track_id) : "");
  };

  const currentAssignFig = figurines.find(f => f.id === assignFigurineId);

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>Tagi NFC</h2>
          <p>Karty i figurki, które dziecko przykłada do urządzenia. Każdy tag może mieć jeden przypisany utwór.</p>
        </div>
      </div>

      <div className="cols-12">
        <div>
          <div className="toolbar">
            <div className="tabs">
              <button className={"tab " + (tabFilter === "all" ? "active" : "")} onClick={() => setTabFilter("all")}>Wszystkie · {figurines.length}</button>
              <button className={"tab " + (tabFilter === "assigned" ? "active" : "")} onClick={() => setTabFilter("assigned")}>Z utworem · {assigned.length}</button>
              <button className={"tab " + (tabFilter === "empty" ? "active" : "")} onClick={() => setTabFilter("empty")}>Puste · {empty.length}</button>
            </div>
          </div>

          <div className="card">
            <div className="row tags-head">
              <div></div>
              <div>Nazwa tagu</div>
              <div>Przypisany utwór</div>
              <div style={{ textAlign: "right" }}>Akcje</div>
            </div>
            <div className="row-list">
              {filtered.length === 0 ? (
                <div style={{ padding: "32px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                  {figurines.length === 0 ? "Brak tagów NFC. Dodaj pierwszy tag." : "Brak wyników."}
                </div>
              ) : filtered.map(f => {
                const song = f.track;
                return (
                  <div key={f.id} className="row tags-row">
                    <div className="tag-thumb figurine"><I.figurine/></div>
                    <div className="cell-title">
                      <div className="t">{f.name}</div>
                      <div className="s" style={{ fontFamily: "var(--font-mono)", fontSize: 11 }}>{f.nfc_uid}</div>
                    </div>
                    <div>
                      {song ? (
                        <div className="t" style={{ fontSize: 14, fontWeight: 500 }}>{song.title}</div>
                      ) : (
                        <span className="chip warn"><I.unlink size={11}/> Brak przypisania</span>
                      )}
                    </div>
                    <div className="row-actions">
                      <button className="btn-icon" title="Przypisz utwór" onClick={() => openAssign(f)}><I.link size={15}/></button>
                      <button className="btn-icon btn-danger" title="Usuń tag" onClick={() => handleDelete(f)}><I.trash size={15}/></button>
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 18 }}>
          <div className="mobile-only"><NfcScanCard onUidFound={uid => setAddUid(uid)}/></div>
          {/* Quick assign card */}
          <div className="card">
            <div className="card-head">
              <h3>Szybkie przypisanie</h3>
            </div>
            <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
              <div className="field">
                <label>Tag / figurka</label>
                <select value={assignFigurineId || ""} onChange={e => {
                  const id = e.target.value ? parseInt(e.target.value) : null;
                  setAssignFigurineId(id);
                  const fig = figurines.find(f => f.id === id);
                  setAssignTrackId(fig?.track_id ? String(fig.track_id) : "");
                }}>
                  <option value="">— wybierz tag —</option>
                  {figurines.map(f => (
                    <option key={f.id} value={f.id}>{f.name}{!f.track_id ? " · brak utworu" : ""}</option>
                  ))}
                </select>
              </div>
              <div style={{ display: "flex", alignItems: "center", justifyContent: "center", color: "var(--ink-4)" }}>
                <I.arrowRight size={16} style={{ transform: "rotate(90deg)" }}/>
              </div>
              <div className="field">
                <label>Utwór</label>
                <select value={assignTrackId} onChange={e => setAssignTrackId(e.target.value)}>
                  <option value="">— usuń przypisanie —</option>
                  {tracks.map(s => (
                    <option key={s.id} value={s.id}>{s.title}</option>
                  ))}
                </select>
              </div>
              <div className="notice">
                <I.info/>
                <div>Po zapisie uruchom <strong>synchronizację</strong>, aby zmiany trafiły na urządzenie.</div>
              </div>
              <button
                className="btn btn-accent"
                style={{ justifyContent: "center" }}
                onClick={handleAssignSave}
                disabled={saving || !assignFigurineId}
              >
                {saving ? <I.sync size={14}/> : <I.check size={14}/>}
                Przypisz utwór
              </button>
            </div>
          </div>

          {/* Add new figurine card */}
          <div className="card">
            <div className="card-head">
              <h3>Dodaj tag ręcznie</h3>
            </div>
            <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
              <div className="field">
                <label>Nazwa</label>
                <input value={addName} onChange={e => setAddName(e.target.value)} placeholder="np. Smok Iggy"/>
              </div>
              <div className="field">
                <label>UID (hex)</label>
                <input value={addUid} onChange={e => setAddUid(e.target.value)} placeholder="04:A2:19:7E:3F:80:02" style={{ fontFamily: "var(--font-mono)" }}/>
              </div>
              {addError && <div className="notice err"><I.alert/> {addError}</div>}
              <button
                className="btn btn-secondary"
                style={{ justifyContent: "center" }}
                onClick={handleAddFigurine}
                disabled={saving || !addName.trim() || !addUid.trim()}
              >
                <I.plus size={14}/> Dodaj tag
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

// ── NFC Scan Card ─────────────────────────────────────────────────────────────

function NfcScanCard({ onUidFound }) {
  const { figurines } = useContextT(AppCtx);
  const nfcAvailable = typeof NDEFReader !== "undefined";
  const secureCtx = window.isSecureContext;
  const [scanState, setScanState] = useStateT("idle"); // idle | scanning | found | error
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
        const existing = figurines.find(f => f.nfc_uid === uid);
        setScannedUid(uid);
        setFoundFig(existing || null);
        setScanState("found");
        if (!existing) onUidFound(uid);
      }, { signal: ctrl.signal });
    } catch (e) {
      if (e.name === "AbortError") { setScanState("idle"); return; }
      setErrMsg(e.message || "Błąd skanowania NFC");
      setScanState("error");
    }
  };

  const stopScan = () => { abortRef.current?.abort(); };
  const reset = () => { setScannedUid(null); setFoundFig(null); setScanState("idle"); };

  // No HTTPS — show instructions first (Chrome hides NDEFReader on HTTP anyway)
  if (!secureCtx) {
    return (
      <div className="card">
        <div className="card-head"><h3>Skanuj NFC</h3><span className="chip warn">wymaga HTTPS</span></div>
        <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <div className="notice warn">
            <I.alert/>
            <div>
              Web NFC wymaga HTTPS. W Chrome na Androidzie wejdź na:<br/>
              <code style={{ display: "block", marginTop: 6, fontSize: 11 }}>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code>
              i dodaj adres tego serwera do listy zaufanych, po czym uruchom ponownie Chrome.
            </div>
          </div>
        </div>
      </div>
    );
  }

  // Not supported (iOS, desktop, etc.)
  if (!nfcAvailable) {
    return (
      <div className="card">
        <div className="card-head"><h3>Skanuj NFC</h3><span className="chip">tylko Android</span></div>
        <div className="card-body">
          <div className="notice"><I.info/><div>Web NFC działa tylko w <strong>Chrome na Androidzie</strong>. Na iPhone wpisz UID ręcznie.</div></div>
        </div>
      </div>
    );
  }

  // Full scan UI
  return (
    <div className="card">
      <div className="card-head">
        <h3>Skanuj NFC</h3>
        {scanState === "scanning" && <span className="chip warn">czekam...</span>}
        {scanState === "found" && <span className="chip ok">odczytano</span>}
      </div>
      <div className="card-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>

        {scanState === "idle" && (
          <button className="btn btn-accent" style={{ justifyContent: "center" }} onClick={startScan}>
            <I.nfc size={16}/> Skanuj tag NFC
          </button>
        )}

        {scanState === "scanning" && (
          <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 16, padding: "16px 0" }}>
            <div className="nfc-pulse"><I.nfc size={32}/></div>
            <div style={{ textAlign: "center", color: "var(--ink-2)", fontSize: "var(--fs-small)", fontWeight: 500 }}>
              Przyłóż figurkę do telefonu
            </div>
            <button className="btn btn-ghost" onClick={stopScan}>Anuluj</button>
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
                <div>Znana figurka: <strong>{foundFig.name}</strong>{foundFig.track ? <> → <em>{foundFig.track.title}</em></> : " (brak przypisanego utworu)"}</div>
              </div>
            ) : (
              <div className="notice">
                <I.info/>
                <div>Nowy tag — UID uzupełniony w formularzu poniżej. Wpisz nazwę i dodaj.</div>
              </div>
            )}
            <button className="btn btn-ghost" style={{ justifyContent: "center" }} onClick={reset}>
              <I.nfc size={14}/> Skanuj ponownie
            </button>
          </div>
        )}

        {scanState === "error" && (
          <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <div className="notice err"><I.alert/> {errMsg}</div>
            <button className="btn btn-ghost" style={{ justifyContent: "center" }} onClick={reset}>Spróbuj ponownie</button>
          </div>
        )}

      </div>
    </div>
  );
}

window.TagsSection = TagsSection;
