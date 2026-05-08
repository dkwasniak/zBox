// Dashboard section
const { useContext: useCDash, useState: useStateDash, useEffect: useEffectDash } = React;

function DashboardSection({ goto, offline }) {
  const { tracks, figurines, sounds, device } = useCDash(AppCtx);
  const { locale, t } = useI18n();
  const [syncCheck, setSyncCheck] = useStateDash(null);
  const [syncCheckLoading, setSyncCheckLoading] = useStateDash(false);

  useEffectDash(() => {
    if (!device) return;
    setSyncCheckLoading(true);
    apiFetch(`/admin/devices/${device.device_id}/sync/check`)
      .then(data => setSyncCheck(data))
      .catch(() => setSyncCheck(null))
      .finally(() => setSyncCheckLoading(false));
  }, [device?.device_id]);

  const tagsWithoutSong = figurines.filter(item => !item.track_id);
  const soundsSet = sounds.filter(item => !!item.filename).length;
  const pendingChanges = syncCheck?.needs_sync ? (syncCheck.upload_count + syncCheck.delete_count) : 0;

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>{t("dashboard.greeting")} 👋</h2>
          <p>{offline
            ? t("dashboard.offlineLead")
            : syncCheck?.needs_sync
              ? t("dashboard.onlineNeedsAttention")
              : t("dashboard.onlineUpToDate")
          }</p>
        </div>
      </div>

      {offline ? (
        <div className="sync-banner offline">
          <div className="icon-blob" style={{ background: "var(--ink-3)", color: "var(--bg-2)" }}><I.power size={22}/></div>
          <div className="text">
            <h4>{t("dashboard.offlineTitle")}</h4>
            <p>{t("dashboard.offlineBody")}</p>
          </div>
          <div className="stack">
            <button className="btn btn-secondary" onClick={() => goto("device")}>{t("common.openDevice")}</button>
          </div>
        </div>
      ) : syncCheck?.needs_sync ? (
        <div className="sync-banner">
          <div className="icon-blob"><I.bolt size={22}/></div>
          <div className="text">
            <h4>{t("dashboard.pendingChanges", { count: pendingChanges })}</h4>
            <p>{syncCheck.message}</p>
          </div>
          <div className="stack">
            <button className="btn btn-secondary" onClick={() => goto("device")}>{t("common.details")}</button>
            <button className="btn btn-primary" onClick={() => goto("device")}><I.bolt size={14}/> {t("common.syncNow")}</button>
          </div>
        </div>
      ) : null}

      <div className="stat-grid">
        <StatTileD lbl={t("common.songs")} val={tracks.length} delta={t("dashboard.tracksInLibrary", { count: tracks.length })} tone="petrol" icon="music" onClick={() => goto("songs")}/>
        <StatTileD lbl={t("common.tags")} val={figurines.length} delta={t("dashboard.tagsUnassigned", { count: tagsWithoutSong.length })} tone="coral" icon="tag" onClick={() => goto("tags")}/>
        <StatTileD lbl={t("common.device")} val={offline ? t("common.offline") : t("common.online")} delta={offline ? t("app.noConnection") : `${device?.ip} · ${device?.mode || "NFC"}`} tone={offline ? "sage" : "ok"} icon="device" onClick={() => goto("device")}/>
        <StatTileD lbl={t("common.sounds")} val={`${soundsSet} / ${sounds.length}`} delta={t("dashboard.systemSoundSlots", { count: sounds.length - soundsSet })} tone="amber" icon="sound" onClick={() => goto("sounds")}/>
      </div>

      <div className="cols-12" style={{ marginTop: 22 }}>
        <div className="card">
          <div className="card-head">
            <h3>{t("dashboard.tagsWithoutTrack")}</h3>
            <span className="sub">{t("dashboard.items", { count: tagsWithoutSong.length })}</span>
            <div className="actions">
              <button className="btn btn-ghost" onClick={() => goto("tags")}>{t("app.browseAllTags")} <I.arrowRight size={13}/></button>
            </div>
          </div>
          <div className="card-body flush">
            {tagsWithoutSong.length === 0 ? (
              <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                {t("dashboard.allTagsAssigned")} 🎉
              </div>
            ) : (
              <div className="row-list">
                {tagsWithoutSong.map(item => (
                  <div key={item.id} className="row" style={{ gridTemplateColumns: "44px 1fr auto" }}>
                    <div className="tag-thumb figurine"><I.figurine/></div>
                    <div className="cell-title">
                      <div className="t">{item.name}</div>
                      <div className="s">{t("dashboard.nfcFigurine")}</div>
                    </div>
                    <button className="btn btn-secondary" onClick={() => goto("tags")}><I.link size={13}/> {t("dashboard.assignTrack")}</button>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        <div className="card">
          <div className="card-head">
            <h3>{t("dashboard.recentlyAdded")}</h3>
            <div className="actions">
              <button className="btn btn-ghost" onClick={() => goto("songs")}>{t("app.browseLibrary")} <I.arrowRight size={13}/></button>
            </div>
          </div>
          <div className="card-body flush">
            {tracks.length === 0 ? (
              <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                {t("dashboard.libraryEmpty")}
              </div>
            ) : (
              <div className="row-list">
                {tracks.slice(0, 4).map(item => (
                  <div key={item.id} className="row" style={{ gridTemplateColumns: "32px 1fr auto" }}>
                    <div className="play-dot" style={{ width: 28, height: 28, pointerEvents: "none" }}><I.music size={11}/></div>
                    <div className="cell-title">
                      <div className="t" style={{ fontSize: 13 }}>{item.title}</div>
                      <div className="s">{fmtDate(item.created_at, locale)}</div>
                    </div>
                    <span className="chip ghost" style={{ fontSize: 10 }}>{item.filename.includes("_trimmed") ? t("dashboard.trimmed") : "MP3"}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function StatTileD({ lbl, val, delta, tone, icon, onClick }) {
  const Icon = I[icon];
  const bg = { ok: "var(--ok-soft)", petrol: "var(--petrol-soft)", amber: "var(--amber-soft)", sage: "var(--sage-soft)", coral: "var(--coral-soft)" }[tone];
  const fg = { ok: "var(--ok)", petrol: "var(--petrol)", amber: "var(--amber)", sage: "var(--sage)", coral: "var(--coral)" }[tone];
  return (
    <button className="stat" onClick={onClick} style={{ textAlign: "left", cursor: "pointer", font: "inherit", border: "1px solid var(--line)" }}>
      <div className="lbl">{lbl}</div>
      <div className="val">{val}</div>
      <div className="delta">{delta}</div>
      <div className="icn-bg" style={{ background: bg, color: fg }}><Icon size={16}/></div>
    </button>
  );
}

window.DashboardSection = DashboardSection;
