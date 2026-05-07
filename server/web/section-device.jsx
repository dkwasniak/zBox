// Device + Sync section
const { useState: useStateDev, useEffect: useEffectDev, useContext: useContextDev, useRef: useRefDev } = React;

function DeviceSection({ offline }) {
  const { device, deviceSettings, refresh, refreshDevice, setDeviceSettings } = useContextDev(AppCtx);
  const [sdFiles, setSdFiles] = useStateDev(null);
  const [sdLoading, setSdLoading] = useStateDev(false);
  const [syncTask, setSyncTask] = useStateDev(null);
  const [syncStarting, setSyncStarting] = useStateDev(false);
  const [ipInput, setIpInput] = useStateDev(deviceSettings.ip || "");
  const [ipSaving, setIpSaving] = useStateDev(false);
  const [logsMeta, setLogsMeta] = useStateDev([]);
  const [logsLoading, setLogsLoading] = useStateDev(false);
  const [logsError, setLogsError] = useStateDev("");
  const [selectedLog, setSelectedLog] = useStateDev("");
  const [logTail, setLogTail] = useStateDev(200);
  const [logContent, setLogContent] = useStateDev("");
  const [logTruncated, setLogTruncated] = useStateDev(false);
  const [logContentLoading, setLogContentLoading] = useStateDev(false);
  const pollRef = useRefDev(null);

  // Load SD files when device is online
  useEffectDev(() => {
    if (!device) { setSdFiles(null); return; }
    setSdLoading(true);
    apiFetch(`/admin/devices/${device.device_id}/files`)
      .then(data => setSdFiles(buildFileTree(data.files)))
      .catch(() => setSdFiles([]))
      .finally(() => setSdLoading(false));
  }, [device?.device_id]);

  useEffectDev(() => {
    if (!device) {
      setLogsMeta([]);
      setSelectedLog("");
      setLogContent("");
      setLogsError("");
      return;
    }
    setLogsLoading(true);
    setLogsError("");
    apiFetch(`/admin/devices/${device.device_id}/logs`)
      .then(data => {
        const logs = data.logs || [];
        setLogsMeta(logs);
        setSelectedLog(current => (current && logs.some(log => log.name === current)) ? current : (logs[0]?.name || ""));
      })
      .catch(e => {
        setLogsMeta([]);
        setSelectedLog("");
        setLogContent("");
        setLogsError(e.message || "Nie udało się pobrać listy logów.");
      })
      .finally(() => setLogsLoading(false));
  }, [device?.device_id]);

  useEffectDev(() => {
    if (!device || !selectedLog) {
      setLogContent("");
      setLogTruncated(false);
      return;
    }
    setLogContentLoading(true);
    setLogsError("");
    apiFetch(`/admin/devices/${device.device_id}/logs/content?name=${encodeURIComponent(selectedLog)}&tail=${logTail}`)
      .then(data => {
        setLogContent(data.text || "");
        setLogTruncated(Boolean(data.truncated));
      })
      .catch(e => {
        setLogContent("");
        setLogTruncated(false);
        setLogsError(e.message || "Nie udało się pobrać treści logu.");
      })
      .finally(() => setLogContentLoading(false));
  }, [device?.device_id, selectedLog, logTail]);

  // Sync task polling
  useEffectDev(() => {
    if (!syncTask || syncTask.status !== "running") return;
    pollRef.current = setInterval(async () => {
      try {
        const status = await apiFetch(`/admin/devices/${device.device_id}/sync/${syncTask.task_id}/status`);
        setSyncTask(status);
        if (status.status !== "running") {
          clearInterval(pollRef.current);
          refreshDevice();
        }
      } catch {
        clearInterval(pollRef.current);
      }
    }, 1500);
    return () => clearInterval(pollRef.current);
  }, [syncTask?.task_id, syncTask?.status]);

  const handleSync = async () => {
    if (!device) return;
    setSyncStarting(true);
    try {
      const res = await apiFetch(`/admin/devices/${device.device_id}/sync`, { method: "POST" });
      const status = await apiFetch(`/admin/devices/${device.device_id}/sync/${res.task_id}/status`);
      setSyncTask(status);
    } catch (e) {
      alert("Błąd synchronizacji: " + e.message);
    } finally {
      setSyncStarting(false);
    }
  };

  const handleRestart = async () => {
    if (!confirm("Zrestartować urządzenie?")) return;
    try {
      await apiFetch(`/admin/devices/${device.device_id}/restart`, { method: "POST" });
    } catch (e) {
      alert("Błąd restartu: " + e.message);
    }
  };

  const handleSaveIp = async () => {
    setIpSaving(true);
    try {
      const res = await apiFetch("/admin/device-settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ip: ipInput }),
      });
      setDeviceSettings(res);
      refreshDevice();
    } catch (e) {
      alert("Błąd zapisu: " + e.message);
    } finally {
      setIpSaving(false);
    }
  };

  const syncBusy = syncTask?.status === "running";

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>Urządzenie i synchronizacja</h2>
          <p>Stan zBoxa, zawartość karty SD i wgranie zmian na urządzenie.</p>
        </div>
        <div className="actions">
          <button className="btn btn-primary"
            onClick={handleSync}
            disabled={offline || syncBusy || syncStarting}
            style={(offline || syncBusy) ? { opacity: 0.45, cursor: "not-allowed" } : {}}>
            {syncBusy || syncStarting ? <I.sync size={14}/> : <I.bolt size={14}/>}
            {syncBusy ? "Synchronizacja..." : "Synchronizuj teraz"}
          </button>
        </div>
      </div>

      {offline ? (
        <OfflineState ipInput={ipInput} setIpInput={setIpInput} onSave={handleSaveIp} ipSaving={ipSaving} onRetry={refreshDevice}/>
      ) : (
        <>
          <div className="stat-grid">
            <StatTile lbl="Połączenie" val="Online" delta={`${device.ip} · ping OK`} tone="ok" icon="wifi"/>
            <StatTile lbl="Tryb pracy" val={device.mode || "NFC"} delta="gotowe do zabawy" tone="petrol" icon="ready"/>
            <StatTile lbl="Bateria" val={`${device.battery_bars}/5`} delta={`${device.battery_v?.toFixed(2) || "?"}V`} tone="amber" icon="battery"/>
            <StatTile lbl="Karta SD" val={device.sd_ok ? "OK" : "Błąd"} delta={device.sd_total ? `${fmtBytes(device.sd_used)} / ${fmtBytes(device.sd_total)}` : "brak danych"} tone="sage" icon="sd"/>
          </div>

          <div className="cols-2-eq">
            <DeviceCard device={device} onRestart={handleRestart}/>
            <SyncCardLive
              syncTask={syncTask}
              syncBusy={syncBusy}
              syncStarting={syncStarting}
              onSync={handleSync}
              offline={offline}
            />
          </div>

          <div style={{ marginTop: 18 }}>
            <SDFilesCard files={sdFiles} loading={sdLoading} onRefresh={() => {
              if (!device) return;
              setSdLoading(true);
              apiFetch(`/admin/devices/${device.device_id}/files`)
                .then(data => setSdFiles(buildFileTree(data.files)))
                .catch(() => setSdFiles([]))
                .finally(() => setSdLoading(false));
            }}/>
          </div>

          <div style={{ marginTop: 18 }}>
            <DiagnosticLogsCard
              device={device}
              logs={logsMeta}
              loading={logsLoading}
              error={logsError}
              selectedLog={selectedLog}
              onSelectLog={setSelectedLog}
              logTail={logTail}
              onTailChange={value => setLogTail(Number(value))}
              content={logContent}
              truncated={logTruncated}
              contentLoading={logContentLoading}
              onRefreshLogs={() => {
                if (!device) return;
                setLogsLoading(true);
                setLogsError("");
                apiFetch(`/admin/devices/${device.device_id}/logs`)
                  .then(data => {
                    const logs = data.logs || [];
                    setLogsMeta(logs);
                    setSelectedLog(current => (current && logs.some(log => log.name === current)) ? current : (logs[0]?.name || ""));
                  })
                  .catch(e => {
                    setLogsMeta([]);
                    setSelectedLog("");
                    setLogContent("");
                    setLogsError(e.message || "Nie udało się pobrać listy logów.");
                  })
                  .finally(() => setLogsLoading(false));
              }}
              onRefreshContent={() => {
                if (!device || !selectedLog) return;
                setLogContentLoading(true);
                setLogsError("");
                apiFetch(`/admin/devices/${device.device_id}/logs/content?name=${encodeURIComponent(selectedLog)}&tail=${logTail}`)
                  .then(data => {
                    setLogContent(data.text || "");
                    setLogTruncated(Boolean(data.truncated));
                  })
                  .catch(e => {
                    setLogContent("");
                    setLogTruncated(false);
                    setLogsError(e.message || "Nie udało się pobrać treści logu.");
                  })
                  .finally(() => setLogContentLoading(false));
              }}
              onDownload={() => {
                if (!device || !selectedLog) return;
                window.location.href = `/admin/devices/${device.device_id}/logs/download?name=${encodeURIComponent(selectedLog)}`;
              }}
            />
          </div>

          {/* IP settings */}
          <div className="card" style={{ marginTop: 18 }}>
            <div className="card-head">
              <h3>Ustawienia połączenia</h3>
            </div>
            <div className="card-body" style={{ display: "flex", gap: 10, alignItems: "flex-end" }}>
              <div className="field" style={{ flex: 1 }}>
                <label>Adres IP urządzenia</label>
                <input value={ipInput} onChange={e => setIpInput(e.target.value)} placeholder="192.168.0.42" style={{ fontFamily: "var(--font-mono)" }}/>
              </div>
              <button className="btn btn-secondary" onClick={handleSaveIp} disabled={ipSaving}>
                {ipSaving ? <I.sync size={14}/> : <I.check size={14}/>} Zapisz
              </button>
              <button className="btn btn-ghost" onClick={refreshDevice}><I.reload size={14}/> Sprawdź</button>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

// ── Offline state ────────────────────────────────────────────────────────────

function OfflineState({ ipInput, setIpInput, onSave, ipSaving, onRetry }) {
  return (
    <div>
      <div className="card" style={{ padding: 0 }}>
        <div style={{ padding: "32px 32px 24px", display: "flex", gap: 24, alignItems: "center", borderBottom: "1px solid var(--line-soft)" }}>
          <div style={{ width: 108, height: 134, borderRadius: 24, background: "linear-gradient(180deg, #F6F8FB 0%, var(--surface-2) 100%)", border: "1px solid var(--line)", position: "relative", flexShrink: 0, boxShadow: "var(--shadow-md)", opacity: 0.85 }}>
            <div style={{ position: "absolute", top: 14, left: "50%", transform: "translateX(-50%)", width: 36, height: 4, borderRadius: 2, background: "var(--ink-4)" }}/>
            <div style={{ position: "absolute", top: 36, left: "50%", transform: "translateX(-50%)", width: 56, height: 56, borderRadius: 18, background: "var(--ink-3)", display: "grid", placeItems: "center", color: "var(--bg-2)" }}>
              <I.power size={26}/>
            </div>
            <div style={{ position: "absolute", bottom: 18, left: "50%", transform: "translateX(-50%)", display: "flex", gap: 6 }}>
              {[0,1,2].map(i => <span key={i} style={{ width: 7, height: 7, borderRadius: "50%", background: "var(--ink-4)" }}/>)}
            </div>
          </div>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 8 }}>
              <span className="chip" style={{ background: "var(--surface-2)", color: "var(--ink-2)" }}>
                <span className="dot" style={{ background: "var(--ink-4)" }}/> offline
              </span>
            </div>
            <div style={{ fontFamily: "var(--font-display)", fontSize: 26, fontWeight: 500, letterSpacing: "-0.02em" }}>
              Nie widzimy zBoxa w sieci
            </div>
            <p style={{ color: "var(--ink-2)", fontSize: 14, margin: "6px 0 0", maxWidth: 540 }}>
              To zwykła sytuacja — urządzenie może być wyłączone, poza zasięgiem WiFi albo otrzymało nowy adres IP.
              Zmiany zostaną zsynchronizowane po przywróceniu połączenia.
            </p>
            <div style={{ display: "flex", gap: 8, marginTop: 16 }}>
              <button className="btn btn-primary" onClick={onRetry}><I.reload size={14}/> Spróbuj ponownie</button>
            </div>
          </div>
        </div>

        <div style={{ padding: "20px 32px" }}>
          <div style={{ fontSize: 11, textTransform: "uppercase", letterSpacing: "0.08em", color: "var(--ink-3)", marginBottom: 14 }}>Co możesz sprawdzić</div>
          <div className="cols-3">
            <CheckTip n="1" title="Zasilanie" body="Upewnij się, że zBox jest włączony i naładowany."/>
            <CheckTip n="2" title="WiFi" body="Urządzenie i ten komputer muszą być w tej samej sieci."/>
            <CheckTip n="3" title="Adres IP" body="Jeśli router przypisał nowy adres, zaktualizuj go poniżej."/>
          </div>
        </div>

        <div style={{ padding: "0 32px 24px", display: "flex", gap: 10, alignItems: "flex-end" }}>
          <div className="field" style={{ flex: 1 }}>
            <label>Adres IP urządzenia</label>
            <input value={ipInput} onChange={e => setIpInput(e.target.value)} placeholder="192.168.0.42" style={{ fontFamily: "var(--font-mono)" }}/>
          </div>
          <button className="btn btn-secondary" onClick={onSave} disabled={ipSaving}>
            {ipSaving ? <I.sync size={14}/> : <I.check size={14}/>} Zapisz i sprawdź
          </button>
        </div>
      </div>
    </div>
  );
}

function CheckTip({ n, title, body }) {
  return (
    <div style={{ background: "var(--bg-2)", border: "1px solid var(--line-soft)", borderRadius: "var(--radius-md)", padding: 14, display: "flex", gap: 12, alignItems: "flex-start" }}>
      <div style={{ width: 24, height: 24, borderRadius: "50%", background: "var(--surface)", border: "1px solid var(--line)", display: "grid", placeItems: "center", fontSize: 11, fontWeight: 700, color: "var(--ink-2)", flexShrink: 0 }}>{n}</div>
      <div>
        <div style={{ fontWeight: 600, fontSize: 13 }}>{title}</div>
        <div style={{ color: "var(--ink-3)", fontSize: 12, marginTop: 2, lineHeight: 1.45 }}>{body}</div>
      </div>
    </div>
  );
}

// ── Device card ──────────────────────────────────────────────────────────────

function StatTile({ lbl, val, delta, tone, icon }) {
  const Icon = I[icon];
  const bg = { ok: "var(--ok-soft)", petrol: "var(--petrol-soft)", amber: "var(--amber-soft)", sage: "var(--sage-soft)" }[tone];
  const fg = { ok: "#3D5C46", petrol: "#1E4347", amber: "#6E521B", sage: "#3F5650" }[tone];
  return (
    <div className="stat">
      <div className="lbl">{lbl}</div>
      <div className="val">{val}</div>
      <div className="delta">{delta}</div>
      <div className="icn-bg" style={{ background: bg, color: fg }}><Icon size={16}/></div>
    </div>
  );
}

function DeviceCard({ device, onRestart }) {
  const sdPct = device.sd_total ? (device.sd_used / device.sd_total) : 0;
  return (
    <div className="card">
      <div className="card-head">
        <h3>Urządzenie</h3>
        <span className="chip ok"><span className="dot live" style={{ marginRight: 4 }}/>online</span>
      </div>
      <div style={{ padding: 22, display: "flex", gap: 22, alignItems: "center", borderBottom: "1px solid var(--line-soft)" }}>
        <div style={{ width: 108, height: 134, borderRadius: 24, background: "linear-gradient(180deg, #2E6BBE 0%, #143E78 100%)", border: "1px solid #1E5BA8", position: "relative", flexShrink: 0, boxShadow: "var(--shadow-md)" }}>
          <div style={{ position: "absolute", top: 14, left: "50%", transform: "translateX(-50%)", width: 36, height: 4, borderRadius: 2, background: "rgba(255,255,255,0.3)" }}/>
          <div style={{ position: "absolute", top: 36, left: "50%", transform: "translateX(-50%)", width: 56, height: 56, borderRadius: 18, background: "#EEF1F6", display: "grid", placeItems: "center", color: "var(--petrol)" }}>
            <I.tag size={26}/>
          </div>
          <div style={{ position: "absolute", bottom: 18, left: "50%", transform: "translateX(-50%)", display: "flex", gap: 6 }}>
            <span style={{ width: 7, height: 7, borderRadius: "50%", background: "var(--ok)" }}/>
            <span style={{ width: 7, height: 7, borderRadius: "50%", background: "rgba(255,255,255,0.3)" }}/>
            <span style={{ width: 7, height: 7, borderRadius: "50%", background: "rgba(255,255,255,0.3)" }}/>
          </div>
        </div>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontFamily: "var(--font-display)", fontSize: 22, fontWeight: 500, letterSpacing: "-0.015em" }}>
            zBox · {device.hostname}
          </div>
          <div style={{ color: "var(--ink-3)", fontSize: 13, marginTop: 2 }}>
            firmware · WiFi {device.rssi} dBm
          </div>
          <div style={{ marginTop: 14, display: "grid", gridTemplateColumns: "1fr 1fr", gap: 10 }}>
            <DeviceField lbl="Adres IP" val={device.ip} mono/>
            <DeviceField lbl="Sygnał" val={device.rssi > -65 ? "dobry" : device.rssi > -80 ? "słaby" : "bardzo słaby"} tone={device.rssi > -65 ? "ok" : "warn"}/>
            <DeviceField lbl="Tryb" val={device.mode || "NFC"}/>
            <DeviceField lbl="Karta SD" val={device.sd_ok ? "OK" : "Błąd"} tone={device.sd_ok ? "ok" : "err"}/>
          </div>
          <div style={{ display: "flex", gap: 8, marginTop: 14 }}>
            <button className="btn btn-ghost btn-danger" onClick={onRestart}><I.power size={14}/> Restart</button>
          </div>
        </div>
      </div>
      {device.sd_total && (
        <div style={{ padding: "16px 22px" }}>
          <div style={{ fontSize: 12, textTransform: "uppercase", letterSpacing: "0.08em", color: "var(--ink-3)", marginBottom: 10 }}>Karta SD</div>
          <div className="sd-bar"><div className="used" style={{ width: `${(sdPct * 100).toFixed(1)}%` }}/></div>
          <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, color: "var(--ink-3)", marginTop: 6 }}>
            <span>{fmtBytes(device.sd_used)} w użyciu</span>
            <span>{fmtBytes(device.sd_total - device.sd_used)} wolnego z {fmtBytes(device.sd_total)}</span>
          </div>
        </div>
      )}
    </div>
  );
}

function DeviceField({ lbl, val, mono, tone }) {
  const color = tone === "ok" ? "var(--ok)" : tone === "warn" ? "var(--warn)" : tone === "err" ? "var(--err)" : "var(--ink)";
  return (
    <div>
      <div style={{ fontSize: 11, textTransform: "uppercase", letterSpacing: "0.06em", color: "var(--ink-3)", marginBottom: 2 }}>{lbl}</div>
      <div style={{ fontSize: 13, fontFamily: mono ? "var(--font-mono)" : "var(--font-ui)", fontWeight: mono ? 500 : 600, color }}>{val}</div>
    </div>
  );
}

// ── Sync Card ────────────────────────────────────────────────────────────────

function SyncCardLive({ syncTask, syncBusy, syncStarting, onSync, offline }) {
  if (!syncTask) {
    return (
      <div className="sync-card">
        <div className="sync-status">
          <div className="sync-orb">
            <I.sync size={28} sw={2}/>
          </div>
          <div className="text">
            <h3>Synchronizacja</h3>
            <p>Kliknij "Synchronizuj teraz", aby wgrać zmiany na urządzenie.</p>
          </div>
        </div>
        <div style={{ padding: "18px 24px" }}>
          <button className="btn btn-accent" style={{ width: "100%", justifyContent: "center" }}
            onClick={onSync} disabled={offline || syncStarting}>
            {syncStarting ? <I.sync size={14}/> : <I.bolt size={14}/>}
            {syncStarting ? "Uruchamianie..." : "Synchronizuj teraz"}
          </button>
        </div>
      </div>
    );
  }

  const { status, progress, message, stage, current_file, current_file_index, current_file_total, uploaded_count, deleted_count, error } = syncTask;

  const steps = [
    { label: "Sprawdzenie zmian",    stages: ["checking", "queued"],          meta: `${syncTask.upload_count || 0} plików` },
    { label: "Mapowania tagów NFC",  stages: ["writing_mappings"],             meta: `${syncTask.mappings_count || 0} tagów` },
    { label: "Przesyłanie plików",   stages: ["uploading"],                    meta: current_file_total ? `${current_file_index || 0} / ${current_file_total}` : "—" },
    { label: "Dźwięki systemowe",    stages: ["writing_system_sounds", "done"], meta: `${syncTask.system_sounds_count || 0} slotów` },
  ];

  const stageOrder = ["queued", "checking", "writing_mappings", "uploading", "writing_system_sounds", "done"];
  const currentIdx = stageOrder.indexOf(stage);

  const getStepStatus = (stepStages) => {
    const stepIdx = Math.max(...stepStages.map(s => stageOrder.indexOf(s)));
    if (status === "done" || status === "completed") return "done";
    if (currentIdx > stepIdx) return "done";
    if (stepStages.includes(stage)) return "now";
    return "todo";
  };

  const isDone = status === "completed" || status === "done";
  const isError = status === "error";

  return (
    <div className="sync-card">
      <div className="sync-status">
        <div className={"sync-orb " + (syncBusy ? "busy" : "")}>
          {isDone ? <I.check size={28} sw={2}/> : isError ? <I.alert size={28}/> : <I.sync size={28} sw={2}/>}
        </div>
        <div className="text">
          <h3>
            {isDone ? "Synchronizacja gotowa" : isError ? "Błąd synchronizacji" : "Synchronizacja w toku"}
          </h3>
          <p>{isError ? (error || "Nieznany błąd") : (message || "Proszę czekać...")}</p>
        </div>
      </div>

      {!isDone && !isError && (
        <div className="sync-progress">
          <div className="sync-pct">
            <span className="pct">{progress || 0}%</span>
            <span className="stage">· {stage || "przygotowanie"}</span>
          </div>
          <div className="bar-track">
            <div className="bar-fill" style={{ width: `${progress || 0}%` }}/>
          </div>
        </div>
      )}

      <div className="sync-meta">
        <div className="cell"><div className="lbl">Przesłane</div><div className="v">{uploaded_count || 0} pliki</div></div>
        <div className="cell"><div className="lbl">Usunięte</div><div className="v">{deleted_count || 0} pliki</div></div>
        <div className="cell"><div className="lbl">Status</div><div className="v">{status}</div></div>
      </div>

      <div className="sync-steps">
        {steps.map((step, i) => {
          const stepStatus = getStepStatus(step.stages);
          return (
            <div key={i} className={"sync-step " + stepStatus}>
              <div className="marker">{stepStatus === "done" ? <I.check size={12} sw={3}/> : i + 1}</div>
              <div>{step.label}</div>
              <div className="meta">{step.meta}</div>
            </div>
          );
        })}
      </div>

      {(isDone || isError) && (
        <div style={{ padding: "0 24px 18px" }}>
          <button className="btn btn-accent" style={{ width: "100%", justifyContent: "center" }} onClick={onSync}>
            <I.reload size={14}/> Synchronizuj ponownie
          </button>
        </div>
      )}
    </div>
  );
}

// ── SD Files ─────────────────────────────────────────────────────────────────

function SDFilesCard({ files, loading, onRefresh }) {
  const [expanded, setExpanded] = useStateDev(() => new Set());

  const toggle = (name) => {
    const next = new Set(expanded);
    if (next.has(name)) next.delete(name); else next.add(name);
    setExpanded(next);
  };

  const iconFor = (type) => {
    if (type === "folder") return I.folder;
    if (type === "audio") return I.music;
    if (type === "config") return I.cog;
    if (type === "log") return I.list;
    return I.file;
  };

  return (
    <div className="card">
      <div className="card-head">
        <h3>Pliki na karcie SD</h3>
        <div className="actions">
          <button className="btn btn-ghost" onClick={onRefresh} disabled={loading}>
            <I.reload size={14}/> Odśwież
          </button>
        </div>
      </div>
      <div className="card-body flush">
        <div className="row files-head">
          <div></div>
          <div>Nazwa</div>
          <div>Typ</div>
          <div>Zawartość</div>
          <div style={{ textAlign: "right" }}>Rozmiar</div>
        </div>
        <div className="row-list">
          {loading && (
            <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
              Ładowanie...
            </div>
          )}
          {!loading && (!files || files.length === 0) && (
            <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
              Brak danych
            </div>
          )}
          {(files || []).map((f, i) => {
            const Icon = iconFor(f.type);
            const isFolder = f.type === "folder";
            const isOpen = isFolder && expanded.has(f.name);
            return (
              <React.Fragment key={i}>
                <div
                  className="row files-row"
                  style={isFolder ? { cursor: "pointer" } : {}}
                  onClick={isFolder ? () => toggle(f.name) : undefined}
                >
                  <div style={{ color: "var(--ink-3)", display: "flex", alignItems: "center", gap: 4 }}>
                    {isFolder ? (
                      <svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round" style={{ transition: "transform 0.15s", transform: isOpen ? "rotate(90deg)" : "rotate(0deg)", flexShrink: 0 }}>
                        <path d="m9 6 6 6-6 6"/>
                      </svg>
                    ) : <span style={{ width: 12, display: "inline-block" }}/>}
                    <Icon size={16}/>
                  </div>
                  <div className="cell-mono" style={{ fontSize: 13, color: "var(--ink)", fontWeight: 500 }}>{f.name}</div>
                  <div><span className="chip ghost">{f.type}</span></div>
                  <div className="muted" style={{ fontSize: 12 }}>{f.count}</div>
                  <div className="cell-num" style={{ textAlign: "right" }}>{f.size}</div>
                </div>
                {isOpen && f.children && f.children.map((c, ci) => {
                  const CIcon = iconFor(c.type);
                  return (
                    <div key={ci} className="row files-row" style={{ background: "var(--bg-2)" }}>
                      <div style={{ color: "var(--ink-4)", display: "flex", alignItems: "center", gap: 4, paddingLeft: 16 }}>
                        <span style={{ width: 12, display: "inline-block" }}/>
                        <CIcon size={14}/>
                      </div>
                      <div className="cell-mono" style={{ fontSize: 12, color: "var(--ink-2)", paddingLeft: 4 }}>{c.name}</div>
                      <div><span className="chip ghost" style={{ fontSize: 10 }}>{c.type}</span></div>
                      <div className="muted" style={{ fontSize: 12 }}>{c.count}</div>
                      <div className="cell-num" style={{ textAlign: "right", fontSize: 12, color: "var(--ink-3)" }}>{c.size}</div>
                    </div>
                  );
                })}
              </React.Fragment>
            );
          })}
        </div>
      </div>
    </div>
  );
}

function DiagnosticLogsCard({
  device,
  logs,
  loading,
  error,
  selectedLog,
  onSelectLog,
  logTail,
  onTailChange,
  content,
  truncated,
  contentLoading,
  onRefreshLogs,
  onRefreshContent,
  onDownload,
}) {
  const selectedMeta = (logs || []).find(log => log.name === selectedLog);

  return (
    <div className="card">
      <div className="card-head">
        <h3>Logi diagnostyczne</h3>
        <div className="actions">
          <button className="btn btn-ghost" onClick={onRefreshLogs} disabled={loading}>
            <I.reload size={14}/> Pliki
          </button>
          <button className="btn btn-ghost" onClick={onRefreshContent} disabled={!selectedLog || contentLoading}>
            <I.reload size={14}/> Podgląd
          </button>
          <button className="btn btn-secondary" onClick={onDownload} disabled={!selectedLog}>
            <I.download size={14}/> Pobierz
          </button>
        </div>
      </div>
      <div className="card-body" style={{ display: "grid", gap: 14 }}>
        <div style={{ display: "grid", gridTemplateColumns: "minmax(0, 1fr) 160px 140px", gap: 10 }}>
          <div className="field">
            <label>Plik logu</label>
            <select value={selectedLog} onChange={e => onSelectLog(e.target.value)} disabled={loading || !(logs || []).length}>
              {!(logs || []).length && <option value="">Brak logów</option>}
              {(logs || []).map(log => <option key={log.name} value={log.name}>{log.name}</option>)}
            </select>
          </div>
          <div className="field">
            <label>Tail</label>
            <select value={String(logTail)} onChange={e => onTailChange(e.target.value)} disabled={!selectedLog}>
              {[100, 200, 400].map(value => <option key={value} value={String(value)}>{value} linii</option>)}
            </select>
          </div>
          <div style={{ alignSelf: "end", color: "var(--ink-3)", fontSize: 12 }}>
            {selectedMeta ? `${fmtBytes(selectedMeta.size)} · ${selectedMeta.mtime ? new Date(selectedMeta.mtime * 1000).toLocaleString("pl-PL") : "brak daty"}` : (device?.sd_ok ? "Brak pliku" : "Karta SD niedostępna")}
          </div>
        </div>

        {error && (
          <div style={{ padding: "12px 14px", borderRadius: "var(--radius-md)", border: "1px solid rgba(174, 75, 61, 0.2)", background: "rgba(174, 75, 61, 0.08)", color: "var(--err)", fontSize: 13 }}>
            {error}
          </div>
        )}

        <div style={{ border: "1px solid var(--line-soft)", borderRadius: "var(--radius-md)", overflow: "hidden", background: "#0F1720" }}>
          <div style={{ padding: "10px 14px", borderBottom: "1px solid rgba(255,255,255,0.08)", color: "rgba(255,255,255,0.72)", fontSize: 12, display: "flex", justifyContent: "space-between", gap: 12 }}>
            <span>{selectedLog || "Podgląd logu"}</span>
            <span>{contentLoading ? "Ładowanie..." : truncated ? "Pokazano końcówkę pliku" : "Pełny zwrócony fragment"}</span>
          </div>
          <pre style={{ margin: 0, padding: 14, minHeight: 220, maxHeight: 420, overflow: "auto", whiteSpace: "pre-wrap", fontFamily: "var(--font-mono)", fontSize: 12, lineHeight: 1.5, color: "#D8E1EB" }}>
            {contentLoading ? "Ładowanie logu..." : (content || "Brak danych do wyświetlenia.")}
          </pre>
        </div>
      </div>
    </div>
  );
}

window.DeviceSection = DeviceSection;
