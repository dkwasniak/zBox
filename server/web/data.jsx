// API utilities and static metadata for zBox admin panel

const API_BASE = "";  // relative — same origin as page

// ── API helpers ──────────────────────────────────────────────────────────────

async function apiFetch(path, opts = {}) {
  const res = await fetch(API_BASE + path, opts);
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(text || `HTTP ${res.status}`);
  }
  if (res.status === 204) return null;
  return res.json();
}

async function apiPost(path, body) {
  return apiFetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

async function apiDelete(path) {
  return apiFetch(path, { method: "DELETE" });
}

// ── Date formatting ──────────────────────────────────────────────────────────

function fmtDate(iso) {
  if (!iso) return "—";
  const d = new Date(iso);
  const now = new Date();
  const diff = (now - d) / 1000;
  if (diff < 60) return "przed chwilą";
  if (diff < 3600) return `${Math.floor(diff / 60)} min temu`;
  if (diff < 86400) return `${Math.floor(diff / 3600)} godz. temu`;
  if (diff < 172800) return "wczoraj";
  if (diff < 604800) return `${Math.floor(diff / 86400)} dni temu`;
  return d.toLocaleDateString("pl-PL");
}

function fmtBytes(bytes) {
  if (!bytes) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1048576) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / 1048576).toFixed(1)} MB`;
}

function fmtPercent(used, total) {
  if (!total) return "0%";
  return `${Math.round((used / total) * 100)}%`;
}

// ── Sound slot static metadata ────────────────────────────────────────────────
// Maps server sound `name` → display info (icon key, tone, human label)

const SOUND_SLOT_META = {
  power_on:   { label: "Włączenie urządzenia", event: "system.power_on",  icon: "power2",     tone: "petrol" },
  power_off:  { label: "Wyłączenie urządzenia", event: "system.power_off", icon: "power",      tone: "coral"  },
  ready:      { label: "Gotowość",              event: "system.ready",     icon: "ready",      tone: "ok"     },
  vol_up:     { label: "Głośność +",            event: "system.vol_up",    icon: "volume",     tone: "petrol" },
  vol_down:   { label: "Głośność −",            event: "system.vol_down",  icon: "volumeMute", tone: "sage"   },
  sync:       { label: "Synchronizacja",         event: "system.sync",      icon: "sync",       tone: "amber"  },
  nfc_mode:   { label: "Tryb NFC",              event: "mode.nfc",         icon: "tag",        tone: "coral"  },
  music_mode: { label: "Tryb Music",            event: "mode.music",       icon: "music",      tone: "petrol" },
};

// ── Pseudo-random waveform ────────────────────────────────────────────────────

function waveBars(n, seed = 7) {
  const out = [];
  let s = seed;
  for (let i = 0; i < n; i++) {
    s = (s * 9301 + 49297) % 233280;
    const r = s / 233280;
    const env = 0.4 + 0.6 * Math.abs(Math.sin((i / n) * Math.PI * 1.4 + 0.6));
    const noise = 0.5 + 0.5 * r;
    out.push(Math.max(0.12, env * noise));
  }
  return out;
}

// ── SD file tree builder ──────────────────────────────────────────────────────

function buildFileTree(flatFiles) {
  const root = [];
  const folders = {};

  (flatFiles || []).forEach(f => {
    const parts = f.path.split("/");
    if (parts.length === 1) {
      // top-level file
      const ext = parts[0].split(".").pop().toLowerCase();
      root.push({
        name: parts[0],
        type: ext === "mp3" ? "audio" : ext === "json" ? "config" : ext === "txt" ? "log" : "file",
        size: fmtBytes(f.size),
        count: "—",
        mtime: f.mtime,
      });
    } else {
      const folderName = parts[0] + "/";
      if (!folders[folderName]) {
        folders[folderName] = { name: folderName, type: "folder", children: [], totalSize: 0, count: 0 };
        root.unshift(folders[folderName]); // folders first
      }
      const ext = parts[parts.length - 1].split(".").pop().toLowerCase();
      folders[folderName].children.push({
        name: parts.slice(1).join("/"),
        type: ext === "mp3" ? "audio" : "file",
        size: fmtBytes(f.size),
        count: "—",
        mtime: f.mtime,
      });
      folders[folderName].totalSize += f.size || 0;
      folders[folderName].count += 1;
    }
  });

  // Format folder sizes
  Object.values(folders).forEach(folder => {
    folder.size = fmtBytes(folder.totalSize);
    folder.count = `${folder.count} pliki`;
  });

  return root;
}

// Export to window for use across JSX files
Object.assign(window, {
  API_BASE, apiFetch, apiPost, apiDelete,
  fmtDate, fmtBytes, fmtPercent,
  SOUND_SLOT_META, waveBars, buildFileTree,
});

// AppCtx — must be defined before section files are parsed
// Sections reference it by name; app.jsx sets window.AppCtx before render
// We create it here so sections can import it without circular deps.
// (Overwritten by app.jsx with the real context after React loads)
// Actual context is attached to window.AppCtx by app.jsx at startup.
