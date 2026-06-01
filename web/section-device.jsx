// Device + Sync section
const { useState: useStateDev, useEffect: useEffectDev, useContext: useContextDev, useRef: useRefDev, useCallback: useCallbackDev } = React;

function DeviceSection({ offline }) {
  const { device, deviceSettings, refresh, refreshDevice, setDeviceSettings, setSyncActive } = useContextDev(AppCtx);
  const { locale, t } = useI18n();
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
      .then(data => setSdFiles(buildFileTree(data.files, locale)))
      .catch(() => setSdFiles([]))
      .finally(() => setSdLoading(false));
  }, [device?.device_id]);

  useEffectDev(() => {
    if (!device) {
      setSyncTask(null);
      setSyncActive(false);
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
        setLogsError(e.message || "Failed to load log list.");
      })
      .finally(() => setLogsLoading(false));
  }, [device?.device_id]);

  useEffectDev(() => {
    setSyncActive(syncTask?.status === "running");
    return () => setSyncActive(false);
  }, [syncTask?.status, setSyncActive]);

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
        setLogsError(e.message || "Failed to load log contents.");
      })
      .finally(() => setLogContentLoading(false));
  }, [device?.device_id, selectedLog, logTail]);

  useEffectDev(() => {
    if (!device) return;
    apiFetch(`/admin/devices/${device.device_id}/sync/current`)
      .then(status => setSyncTask(status))
      .catch(() => setSyncTask(null));
  }, [device?.device_id]);

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
      alert(t("device.syncErrorAlert", { message: e.message }));
    } finally {
      setSyncStarting(false);
    }
  };

  const handleRestart = async () => {
    if (!confirm(t("device.restartConfirm"))) return;
    try {
      await apiFetch(`/admin/devices/${device.device_id}/restart`, { method: "POST" });
    } catch (e) {
      alert(t("device.restartErrorAlert", { message: e.message }));
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
      alert(t("device.saveErrorAlert", { message: e.message }));
    } finally {
      setIpSaving(false);
    }
  };

  const syncBusy = syncTask?.status === "running";

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>{t("device.heading")}</h2>
          <p>{t("device.description")}</p>
        </div>
        <div className="actions">
          <button className="btn btn-primary"
            onClick={handleSync}
            disabled={offline || syncBusy || syncStarting}
            style={(offline || syncBusy) ? { opacity: 0.45, cursor: "not-allowed" } : {}}>
            {syncBusy || syncStarting ? <I.sync size={14}/> : <I.bolt size={14}/>}
            {syncBusy ? t("device.syncing") : t("common.syncNow")}
          </button>
        </div>
      </div>

      {offline ? (
        <OfflineState ipInput={ipInput} setIpInput={setIpInput} onSave={handleSaveIp} ipSaving={ipSaving} onRetry={refreshDevice}/>
      ) : (
        <>
          <div className="stat-grid">
            <StatTile lbl={t("common.connection")} val={t("common.online")} delta={`${device.ip} · ${t("device.pingOk")}`} tone="ok" icon="wifi"/>
            <StatTile lbl={t("common.mode")} val={device.mode || "NFC"} delta={t("device.readyToUse")} tone="petrol" icon="ready"/>
            <StatTile lbl={t("common.battery")} val={`${device.battery_bars}/5`} delta={`${device.battery_v?.toFixed(2) || "?"}V`} tone="amber" icon="battery"/>
            <StatTile lbl={t("common.sdCard")} val={device.sd_ok ? "OK" : "Error"} delta={device.sd_total ? `${fmtBytes(device.sd_used)} / ${fmtBytes(device.sd_total)}` : t("device.noSdData")} tone="sage" icon="sd"/>
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
            <BtPairingCard device={device}/>
          </div>

          <div style={{ marginTop: 18 }}>
            <SDFilesCard files={sdFiles} loading={sdLoading} onRefresh={() => {
              if (!device) return;
              setSdLoading(true);
              apiFetch(`/admin/devices/${device.device_id}/files`)
                .then(data => setSdFiles(buildFileTree(data.files, locale)))
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
                    setLogsError(e.message || "Failed to load log list.");
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
                    setLogsError(e.message || "Failed to load log contents.");
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
              <h3>{t("device.ipSettings")}</h3>
            </div>
            <div className="card-body" style={{ display: "flex", gap: 10, alignItems: "flex-end" }}>
              <div className="field" style={{ flex: 1 }}>
                <label>{t("common.ipAddress")}</label>
                <input value={ipInput} onChange={e => setIpInput(e.target.value)} placeholder="192.168.0.42" style={{ fontFamily: "var(--font-mono)" }}/>
              </div>
              <button className="btn btn-secondary" onClick={handleSaveIp} disabled={ipSaving}>
                {ipSaving ? <I.sync size={14}/> : <I.check size={14}/>} {t("common.save")}
              </button>
              <button className="btn btn-ghost" onClick={refreshDevice}><I.reload size={14}/> {t("common.check")}</button>
            </div>
          </div>
        </>
      )}
    </div>
  );
}

// ── Offline state ────────────────────────────────────────────────────────────

function OfflineState({ ipInput, setIpInput, onSave, ipSaving, onRetry }) {
  const { t } = useI18n();
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
                <span className="dot" style={{ background: "var(--ink-4)" }}/> {t("common.offline").toLowerCase()}
              </span>
            </div>
            <div style={{ fontFamily: "var(--font-display)", fontSize: 26, fontWeight: 500, letterSpacing: "-0.02em" }}>
              {t("device.offlineTitle")}
            </div>
            <p style={{ color: "var(--ink-2)", fontSize: 14, margin: "6px 0 0", maxWidth: 540 }}>
              {t("device.offlineBody")}
            </p>
            <div style={{ display: "flex", gap: 8, marginTop: 16 }}>
              <button className="btn btn-primary" onClick={onRetry}><I.reload size={14}/> {t("common.retry")}</button>
            </div>
          </div>
        </div>

        <div style={{ padding: "20px 32px" }}>
          <div style={{ fontSize: 11, textTransform: "uppercase", letterSpacing: "0.08em", color: "var(--ink-3)", marginBottom: 14 }}>{t("device.whatToCheck")}</div>
          <div className="cols-3">
            <CheckTip n="1" title={t("device.power")} body={t("device.powerHint")}/>
            <CheckTip n="2" title={t("device.wifi")} body={t("device.wifiHint")}/>
            <CheckTip n="3" title={t("common.ipAddress")} body={t("device.ipHint")}/>
          </div>
        </div>

        <div style={{ padding: "0 32px 24px", display: "flex", gap: 10, alignItems: "flex-end" }}>
          <div className="field" style={{ flex: 1 }}>
            <label>{t("common.ipAddress")}</label>
            <input value={ipInput} onChange={e => setIpInput(e.target.value)} placeholder="192.168.0.42" style={{ fontFamily: "var(--font-mono)" }}/>
          </div>
          <button className="btn btn-secondary" onClick={onSave} disabled={ipSaving}>
            {ipSaving ? <I.sync size={14}/> : <I.check size={14}/>} {t("common.saveAndCheck")}
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
  const { t } = useI18n();
  const sdPct = device.sd_total ? (device.sd_used / device.sd_total) : 0;
  return (
    <div className="card">
      <div className="card-head">
        <h3>{t("device.deviceCard")}</h3>
        <span className="chip ok"><span className="dot live" style={{ marginRight: 4 }}/>{t("common.online").toLowerCase()}</span>
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
            firmware · Wi-Fi {device.rssi} dBm
          </div>
          <div style={{ marginTop: 14, display: "grid", gridTemplateColumns: "1fr 1fr", gap: 10 }}>
            <DeviceField lbl={t("common.ipAddress")} val={device.ip} mono/>
            <DeviceField lbl="Signal" val={device.rssi > -65 ? t("device.signalGood") : device.rssi > -80 ? t("device.signalWeak") : t("device.signalVeryWeak")} tone={device.rssi > -65 ? "ok" : "warn"}/>
            <DeviceField lbl={t("common.mode")} val={device.mode || "NFC"}/>
            <DeviceField lbl={t("common.sdCard")} val={device.sd_ok ? "OK" : "Error"} tone={device.sd_ok ? "ok" : "err"}/>
          </div>
          <div style={{ display: "flex", gap: 8, marginTop: 14 }}>
            <button className="btn btn-ghost btn-danger" onClick={onRestart}><I.power size={14}/> {t("common.restart")}</button>
          </div>
        </div>
      </div>
      {device.sd_total && (
        <div style={{ padding: "16px 22px" }}>
          <div style={{ fontSize: 12, textTransform: "uppercase", letterSpacing: "0.08em", color: "var(--ink-3)", marginBottom: 10 }}>{t("common.sdCard")}</div>
          <div className="sd-bar"><div className="used" style={{ width: `${(sdPct * 100).toFixed(1)}%` }}/></div>
          <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, color: "var(--ink-3)", marginTop: 6 }}>
            <span>{fmtBytes(device.sd_used)} {t("device.inUse")}</span>
            <span>{t("device.freeOf", { free: fmtBytes(device.sd_total - device.sd_used), total: fmtBytes(device.sd_total) })}</span>
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
  const { locale, t } = useI18n();
  if (!syncTask) {
    return (
      <div className="sync-card">
        <div className="sync-status">
          <div className="sync-orb">
            <I.sync size={28} sw={2}/>
          </div>
          <div className="text">
            <h3>{t("device.syncCard")}</h3>
            <p>{t("device.syncPrompt")}</p>
          </div>
        </div>
        <div style={{ padding: "18px 24px" }}>
          <button className="btn btn-accent" style={{ width: "100%", justifyContent: "center" }}
            onClick={onSync} disabled={offline || syncStarting}>
            {syncStarting ? <I.sync size={14}/> : <I.bolt size={14}/>}
            {syncStarting ? t("device.syncStarting") : t("common.syncNow")}
          </button>
        </div>
      </div>
    );
  }

  const {
    status,
    progress,
    message,
    stage,
    current_file,
    current_file_index,
    current_file_total,
    current_file_progress,
    uploaded_count,
    deleted_count,
    error,
    sync_files,
  } = syncTask;

  const steps = [
    { label: t("device.checkChanges"),   stages: ["fetching_device_state", "comparing_files", "queued"], meta: t("device.filesWord", { count: syncTask.upload_count || 0 }) },
    { label: t("device.tagMappings"),    stages: ["writing_mappings"],             meta: t("device.tagsWord", { count: syncTask.mappings_count || 0 }) },
    { label: t("device.uploadFiles"),    stages: ["uploading_audio", "deleting_remote"], meta: current_file_total ? `${current_file_index || 0} / ${current_file_total}` : "—" },
    { label: t("device.systemSounds"),   stages: ["writing_system_sounds", "completed"], meta: t("device.slotsWord", { count: syncTask.system_sounds_count || 0 }) },
  ];

  const stageOrder = ["queued", "fetching_device_state", "comparing_files", "uploading_audio", "deleting_remote", "writing_mappings", "writing_system_sounds", "writing_led_config", "completed"];
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
  const fileItems = sync_files || [];

  const syncFileLabel = (item) => {
    if (item.status === "uploaded") return localeLabel("gotowe", "done");
    if (item.status === "uploading") return item.progress != null ? `${item.progress}%` : localeLabel("w toku", "in progress");
    return localeLabel("oczekuje", "pending");
  };

  function localeLabel(pl, en) {
    return locale === "pl" ? pl : en;
  }

  return (
    <div className="sync-card">
      <div className="sync-status">
        <div className={"sync-orb " + (syncBusy ? "busy" : "")}>
          {isDone ? <I.check size={28} sw={2}/> : isError ? <I.alert size={28}/> : <I.sync size={28} sw={2}/>}
        </div>
        <div className="text">
          <h3>
            {isDone ? t("device.syncReady") : isError ? t("device.syncError") : t("device.syncRunning")}
          </h3>
          <p>{isError ? (error || t("device.unknownError")) : (message || t("device.pleaseWait"))}</p>
        </div>
      </div>

      {!isDone && !isError && (
        <div className="sync-progress">
          <div className="sync-pct">
            <span className="pct">{progress || 0}%</span>
            <span className="stage">· {stage || "queued"}</span>
          </div>
          <div className="bar-track">
            <div className="bar-fill" style={{ width: `${progress || 0}%` }}/>
          </div>
        </div>
      )}

      <div className="sync-meta">
        <div className="cell"><div className="lbl">{t("common.uploaded")}</div><div className="v">{t("device.filesWord", { count: uploaded_count || 0 })}</div></div>
        <div className="cell"><div className="lbl">{t("common.deletedFiles")}</div><div className="v">{t("device.filesWord", { count: deleted_count || 0 })}</div></div>
        <div className="cell"><div className="lbl">{t("common.status")}</div><div className="v">{status}</div></div>
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

      {!!fileItems.length && (
        <div style={{ padding: "8px 24px 18px" }}>
          <div style={{ fontSize: "var(--fs-caption)", color: "var(--ink-3)", marginBottom: 10 }}>
            {t("device.uploadFiles")} · {t("device.filesWord", { count: fileItems.length })}
          </div>
          <div className="sync-file-list">
            {fileItems.map((item) => {
              const isCurrent = item.path === current_file && status === "running";
              const itemProgress = isCurrent ? current_file_progress : item.progress;
              return (
                <div key={item.path} className={"sync-file-row " + item.status}>
                  <div className="sync-file-main">
                    <div className="sync-file-path">{item.path}</div>
                    <div className="sync-file-state">{syncFileLabel({ ...item, progress: itemProgress })}</div>
                  </div>
                  <div className="sync-file-bar">
                    <div
                      className="sync-file-bar-fill"
                      style={{ width: `${item.status === "uploaded" ? 100 : Math.max(0, Math.min(100, itemProgress || 0))}%` }}
                    />
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}

      {(isDone || isError) && (
        <div style={{ padding: "0 24px 18px" }}>
          <button className="btn btn-accent" style={{ width: "100%", justifyContent: "center" }} onClick={onSync}>
            <I.reload size={14}/> {t("device.syncAgain")}
          </button>
        </div>
      )}
    </div>
  );
}

// ── SD Files ─────────────────────────────────────────────────────────────────

function SDFilesCard({ files, loading, onRefresh }) {
  const { t } = useI18n();
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
        <h3>{t("device.sdFiles")}</h3>
        <div className="actions">
          <button className="btn btn-ghost" onClick={onRefresh} disabled={loading}>
            <I.reload size={14}/> {t("common.refresh")}
          </button>
        </div>
      </div>
      <div className="card-body flush">
        <div className="row files-head">
          <div></div>
          <div>{t("tags.name")}</div>
          <div>Typ</div>
          <div>{t("device.contents")}</div>
          <div style={{ textAlign: "right" }}>Size</div>
        </div>
        <div className="row-list">
          {loading && (
            <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
              {t("common.loading")}
            </div>
          )}
          {!loading && (!files || files.length === 0) && (
            <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
              {t("common.noData")}
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
  const { locale, t } = useI18n();
  const selectedMeta = (logs || []).find(log => log.name === selectedLog);

  return (
    <div className="card">
      <div className="card-head">
        <h3>{t("device.logFiles")}</h3>
        <div className="actions">
          <button className="btn btn-ghost" onClick={onRefreshLogs} disabled={loading}>
            <I.reload size={14}/> {t("common.files")}
          </button>
          <button className="btn btn-ghost" onClick={onRefreshContent} disabled={!selectedLog || contentLoading}>
            <I.reload size={14}/> {t("common.preview")}
          </button>
          <button className="btn btn-secondary" onClick={onDownload} disabled={!selectedLog}>
            <I.download size={14}/> {t("common.download")}
          </button>
        </div>
      </div>
      <div className="card-body" style={{ display: "grid", gap: 14 }}>
        <div style={{ display: "grid", gridTemplateColumns: "minmax(0, 1fr) 160px 140px", gap: 10 }}>
          <div className="field">
            <label>{t("device.logFile")}</label>
            <select value={selectedLog} onChange={e => onSelectLog(e.target.value)} disabled={loading || !(logs || []).length}>
              {!(logs || []).length && <option value="">{t("app.noLogs")}</option>}
              {(logs || []).map(log => <option key={log.name} value={log.name}>{log.name}</option>)}
            </select>
          </div>
          <div className="field">
            <label>{t("device.fileTail")}</label>
            <select value={String(logTail)} onChange={e => onTailChange(e.target.value)} disabled={!selectedLog}>
              {[100, 200, 400].map(value => <option key={value} value={String(value)}>{t("app.logLines", { value })}</option>)}
            </select>
          </div>
          <div style={{ alignSelf: "end", color: "var(--ink-3)", fontSize: 12 }}>
            {selectedMeta ? `${fmtBytes(selectedMeta.size)} · ${selectedMeta.mtime ? fmtDateTime(new Date(selectedMeta.mtime * 1000), locale) : t("app.emptyDate")}` : (device?.sd_ok ? t("app.noFile") : t("device.cardUnavailable"))}
          </div>
        </div>

        {error && (
          <div style={{ padding: "12px 14px", borderRadius: "var(--radius-md)", border: "1px solid rgba(174, 75, 61, 0.2)", background: "rgba(174, 75, 61, 0.08)", color: "var(--err)", fontSize: 13 }}>
            {error}
          </div>
        )}

        <div style={{ border: "1px solid var(--line-soft)", borderRadius: "var(--radius-md)", overflow: "hidden", background: "#0F1720" }}>
          <div style={{ padding: "10px 14px", borderBottom: "1px solid rgba(255,255,255,0.08)", color: "rgba(255,255,255,0.72)", fontSize: 12, display: "flex", justifyContent: "space-between", gap: 12 }}>
            <span>{selectedLog || t("app.logPreview")}</span>
            <span>{contentLoading ? t("common.loading") : truncated ? t("app.tailFragment") : t("app.fullFragment")}</span>
          </div>
          <pre style={{ margin: 0, padding: 14, minHeight: 220, maxHeight: 420, overflow: "auto", whiteSpace: "pre-wrap", fontFamily: "var(--font-mono)", fontSize: 12, lineHeight: 1.5, color: "#D8E1EB" }}>
            {contentLoading ? t("device.loadingLog") : (content || t("device.noLogData"))}
          </pre>
        </div>
      </div>
    </div>
  );
}

// ── BT Pairing Card ──────────────────────────────────────────────────────────

function BtPairingCard({ device }) {
  const { t } = useI18n();
  const [btDevices, setBtDevices] = useStateDev([]);
  const [btScanning, setBtScanning] = useStateDev(false);
  const [btSaving, setBtSaving] = useStateDev(null); // mac being saved, or null
  const [btSaved, setBtSaved] = useStateDev(null);   // last saved name
  const [btError, setBtError] = useStateDev("");
  const [btLog, setBtLog] = useStateDev([]);
  const btLogSinceRef = useRefDev(0);
  const logPollRef = useRefDev(null);
  const logEndRef = useRefDev(null);
  const pollRef = useRefDev(null);

  const stopPolling = useCallbackDev(() => {
    if (pollRef.current) { clearInterval(pollRef.current); pollRef.current = null; }
    if (logPollRef.current) { clearInterval(logPollRef.current); logPollRef.current = null; }
  }, []);

  const fetchDevices = useCallbackDev(async () => {
    try {
      const data = await apiFetch(`/admin/devices/${device.device_id}/bt/devices`);
      setBtDevices(prev => {
        // Merge: keep existing entries, append new ones (dedup by mac)
        const existingMacs = new Set(prev.map(d => d.mac));
        const newEntries = (data.devices || []).filter(d => !existingMacs.has(d.mac));
        return [...prev, ...newEntries];
      });
      // Don't auto-stop on scanning=false — firmware restarts inquiry automatically.
      // User controls stop via the Stop button.
    } catch {
      // Poll failures are expected (WiFi+BT radio contention, transient device restarts).
    }
  }, [device.device_id]);

  const pollLog = useCallbackDev(async () => {
    try {
      const data = await apiFetch(
        `/admin/devices/${device.device_id}/diag/recent-log?since=${btLogSinceRef.current}`
      );
      if (data.total < btLogSinceRef.current) {
        // Device restarted — reset cursor and show a separator in the log
        btLogSinceRef.current = 0;
        setBtLog(prev => [...prev, "── restart urządzenia ──"]);
      }
      if (data.lines && data.lines.length > 0) {
        setBtLog(prev => [...prev, ...data.lines]);
        btLogSinceRef.current = data.total;
      }
    } catch {}
  }, [device.device_id]);

  useEffectDev(() => {
    if (logEndRef.current) logEndRef.current.scrollIntoView({ behavior: "smooth" });
  }, [btLog]);

  useEffectDev(() => {
    if (btScanning) {
      pollRef.current = setInterval(fetchDevices, 3000);
      logPollRef.current = setInterval(pollLog, 2000);
    }
    return stopPolling;
  }, [btScanning, fetchDevices, pollLog, stopPolling]);

  const handleStartScan = async () => {
    setBtError("");
    setBtDevices([]);
    setBtLog([]);
    btLogSinceRef.current = 0;
    try {
      await apiFetch(`/admin/devices/${device.device_id}/bt/start-scan`, { method: "POST" });
      await Promise.allSettled([fetchDevices(), pollLog()]);
      setBtScanning(true);
    } catch (e) {
      setBtError(e.message || "Nie udało się uruchomić skanowania BT");
    }
  };

  const handleStopScan = async () => {
    stopPolling();
    setBtScanning(false);
    try {
      await apiFetch(`/admin/devices/${device.device_id}/bt/stop-scan`, { method: "POST" });
    } catch {}
  };

  const handleSelect = async (deviceEntry) => {
    const name = deviceEntry.name;
    const mac = deviceEntry.mac || "";
    stopPolling();
    setBtScanning(false);
    setBtSaving(mac || name);
    setBtError("");
    try {
      await apiFetch(`/admin/devices/${device.device_id}/bt/select`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, mac }),
      });
      setBtSaved(name);
    } catch (e) {
      setBtError(e.message || "Nie udało się zapisać urządzenia");
    } finally {
      setBtSaving(null);
    }
  };

  const currentTarget = btSaved || "zBox Headphones";

  return (
    <div className="card">
      <div className="card-head">
        <h3>Głośnik Bluetooth</h3>
      </div>
      <div className="card-body" style={{ display: "grid", gap: 14 }}>

        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <div>
            <div style={{ fontSize: 11, textTransform: "uppercase", letterSpacing: "0.06em", color: "var(--ink-3)", marginBottom: 2 }}>Aktualnie</div>
            <div style={{ fontWeight: 600, fontSize: 14 }}>
              {currentTarget}
              {btSaved && <span style={{ marginLeft: 8, color: "var(--ok)", fontSize: 12 }}>✓ zapisano</span>}
            </div>
          </div>
          <div style={{ display: "flex", gap: 8 }}>
            {btScanning ? (
              <button className="btn btn-ghost" onClick={handleStopScan}>
                <I.stop size={14}/> Zatrzymaj
              </button>
            ) : (
              <button className="btn btn-secondary" onClick={handleStartScan} disabled={!!btSaving}>
                <I.bt size={14}/> Szukaj urządzeń BT
              </button>
            )}
          </div>
        </div>

        {btScanning && (
          <div style={{ display: "flex", alignItems: "center", gap: 8, color: "var(--ink-2)", fontSize: 13 }}>
            <I.sync size={14}/> Wyszukiwanie urządzeń w pobliżu...
            <span style={{ color: "var(--ink-3)", fontSize: 12 }}>Włącz głośnik w trybie parowania</span>
          </div>
        )}

        {btError && (
          <div style={{ padding: "10px 12px", borderRadius: "var(--radius-md)", background: "rgba(174,75,61,0.08)", border: "1px solid rgba(174,75,61,0.2)", color: "var(--err)", fontSize: 13 }}>
            {btError}
          </div>
        )}

        {btDevices.length > 0 && (
          <div>
            <div style={{ fontSize: 11, textTransform: "uppercase", letterSpacing: "0.06em", color: "var(--ink-3)", marginBottom: 8 }}>
              Znalezione urządzenia ({btDevices.length})
            </div>
            <div className="row-list">
              {btDevices.map((d) => {
                const isCurrent = d.name === currentTarget;
                const isSaving = btSaving === (d.mac || d.name);
                return (
                  <div key={d.mac} className="row" style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "10px 0" }}>
                    <div>
                      <div style={{ fontWeight: 600, fontSize: 13 }}>{d.name}</div>
                      <div style={{ fontFamily: "var(--font-mono)", fontSize: 11, color: "var(--ink-3)" }}>{d.mac}</div>
                    </div>
                    {isCurrent ? (
                      <span className="chip ok"><span className="dot" style={{ marginRight: 4 }}/>aktualny</span>
                    ) : (
                      <button className="btn btn-secondary" onClick={() => handleSelect(d)} disabled={!!btSaving}>
                        {isSaving ? <I.sync size={13}/> : <I.bt size={13}/>} Paruj
                      </button>
                    )}
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {!btScanning && btDevices.length === 0 && !btError && (
          <div style={{ color: "var(--ink-3)", fontSize: 13 }}>
            Naciśnij „Szukaj urządzeń BT" aby zobaczyć dostępne głośniki.
            Zmiana aktywna po restarcie urządzenia.
          </div>
        )}

        {btLog.length > 0 && (
          <div style={{ marginTop: 8 }}>
            <div style={{ fontSize: 11, color: "var(--text-muted)", marginBottom: 4 }}>Log urządzenia</div>
            <div
              style={{
                background: "var(--bg-secondary, #1a1a1a)",
                borderRadius: 6,
                padding: "8px 10px",
                fontFamily: "monospace",
                fontSize: 11,
                color: "var(--text-muted, #aaa)",
                maxHeight: 180,
                overflowY: "auto",
                whiteSpace: "pre-wrap",
                wordBreak: "break-all",
              }}
            >
              {btLog.map((line, i) => (
                <div key={i} style={{ color: line.includes("[BT]") ? "var(--accent, #4af)" : undefined }}>
                  {line}
                </div>
              ))}
              <div ref={logEndRef} />
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

window.DeviceSection = DeviceSection;
