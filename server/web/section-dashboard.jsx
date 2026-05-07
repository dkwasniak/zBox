// Dashboard section
const { useContext: useCDash, useState: useStateDash, useEffect: useEffectDash } = React;

function DashboardSection({ goto, offline }) {
  const { tracks, figurines, sounds, device } = useCDash(AppCtx);
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

  const tagsWithoutSong = figurines.filter(f => !f.track_id);
  const soundsSet = sounds.filter(s => !!s.filename).length;
  const pendingChanges = syncCheck?.needs_sync
    ? (syncCheck.upload_count + syncCheck.delete_count)
    : 0;

  return (
    <div>
      <div className="section-head">
        <div>
          <h2>Dzień dobry 👋</h2>
          <p>{offline
            ? "zBox jest teraz offline. Możesz spokojnie przygotować zmiany — wgramy je przy następnym połączeniu."
            : syncCheck?.needs_sync
              ? "zBox jest online. Kilka rzeczy czeka na uwagę."
              : "zBox jest online. Wszystko zsynchronizowane."
          }</p>
        </div>
      </div>

      {offline ? (
        <div className="sync-banner offline">
          <div className="icon-blob" style={{ background: "var(--ink-3)", color: "var(--bg-2)" }}><I.power size={22}/></div>
          <div className="text">
            <h4>zBox jest offline</h4>
            <p>Sprawdź zasilanie i WiFi urządzenia. Zmiany zostaną zsynchronizowane automatycznie po połączeniu.</p>
          </div>
          <div className="stack">
            <button className="btn btn-secondary" onClick={() => goto("device")}>Otwórz urządzenie</button>
          </div>
        </div>
      ) : syncCheck?.needs_sync ? (
        <div className="sync-banner">
          <div className="icon-blob"><I.bolt size={22}/></div>
          <div className="text">
            <h4>{pendingChanges} {pendingChanges === 1 ? "zmiana czeka" : "zmiany czekają"} na wgranie</h4>
            <p>{syncCheck.message}</p>
          </div>
          <div className="stack">
            <button className="btn btn-secondary" onClick={() => goto("device")}>Zobacz szczegóły</button>
            <button className="btn btn-primary" onClick={() => goto("device")}><I.bolt size={14}/> Synchronizuj</button>
          </div>
        </div>
      ) : null}

      <div className="stat-grid">
        <StatTileD lbl="Utwory" val={tracks.length} delta={tracks.length === 0 ? "brak utworów" : `${tracks.length} w bibliotece`} tone="petrol" icon="music" onClick={() => goto("songs")}/>
        <StatTileD lbl="Tagi NFC" val={figurines.length} delta={`${tagsWithoutSong.length} bez przypisania`} tone="coral" icon="tag" onClick={() => goto("tags")}/>
        <StatTileD lbl="Urządzenie" val={offline ? "Offline" : "Online"} delta={offline ? "brak połączenia" : `${device?.ip} · ${device?.mode || "NFC"}`} tone={offline ? "sage" : "ok"} icon="device" onClick={() => goto("device")}/>
        <StatTileD lbl="Dźwięki systemowe" val={`${soundsSet} / ${sounds.length}`} delta={`${sounds.length - soundsSet} slotów pustych`} tone="amber" icon="sound" onClick={() => goto("sounds")}/>
      </div>

      <div className="cols-12" style={{ marginTop: 22 }}>
        <div className="card">
          <div className="card-head">
            <h3>Tagi bez utworu</h3>
            <span className="sub">{tagsWithoutSong.length} {tagsWithoutSong.length === 1 ? "pozycja" : "pozycji"}</span>
            <div className="actions">
              <button className="btn btn-ghost" onClick={() => goto("tags")}>Wszystkie tagi <I.arrowRight size={13}/></button>
            </div>
          </div>
          <div className="card-body flush">
            {tagsWithoutSong.length === 0 ? (
              <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                Wszystkie tagi mają przypisane utwory 🎉
              </div>
            ) : (
              <div className="row-list">
                {tagsWithoutSong.map(f => (
                  <div key={f.id} className="row" style={{ gridTemplateColumns: "44px 1fr auto" }}>
                    <div className="tag-thumb figurine"><I.figurine/></div>
                    <div className="cell-title">
                      <div className="t">{f.name}</div>
                      <div className="s">Figurka NFC</div>
                    </div>
                    <button className="btn btn-secondary" onClick={() => goto("tags")}><I.link size={13}/> Przypisz utwór</button>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        <div className="card">
          <div className="card-head">
            <h3>Ostatnio dodane</h3>
            <div className="actions">
              <button className="btn btn-ghost" onClick={() => goto("songs")}>Biblioteka <I.arrowRight size={13}/></button>
            </div>
          </div>
          <div className="card-body flush">
            {tracks.length === 0 ? (
              <div style={{ padding: "24px", textAlign: "center", color: "var(--ink-3)", fontSize: "var(--fs-small)" }}>
                Brak utworów w bibliotece
              </div>
            ) : (
              <div className="row-list">
                {tracks.slice(0, 4).map(s => (
                  <div key={s.id} className="row" style={{ gridTemplateColumns: "32px 1fr auto" }}>
                    <div className="play-dot" style={{ width: 28, height: 28, pointerEvents: "none" }}><I.music size={11}/></div>
                    <div className="cell-title">
                      <div className="t" style={{ fontSize: 13 }}>{s.title}</div>
                      <div className="s">{fmtDate(s.created_at)}</div>
                    </div>
                    <span className="chip ghost" style={{ fontSize: 10 }}>{s.filename.includes("_trimmed") ? "przycięty" : "MP3"}</span>
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
