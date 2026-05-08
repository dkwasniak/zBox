// Lucide-style line icons. All 24x24 viewBox, currentColor.
const Ic = ({ d, children, size = 18, sw = 1.75, fill = "none", style }) => (
  <svg
    viewBox="0 0 24 24"
    width={size}
    height={size}
    fill={fill}
    stroke="currentColor"
    strokeWidth={sw}
    strokeLinecap="round"
    strokeLinejoin="round"
    className="icn"
    style={style}
    aria-hidden="true"
  >
    {d ? <path d={d} /> : children}
  </svg>
);

const I = {
  music: (p) => <Ic {...p}><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></Ic>,
  tag: (p) => <Ic {...p}><path d="M20.59 13.41 13 21l-9-9V4h8l8.59 8.59a2 2 0 0 1 0 2.82Z"/><circle cx="8" cy="8" r="1.5" fill="currentColor"/></Ic>,
  device: (p) => <Ic {...p}><rect x="5" y="3" width="14" height="18" rx="3"/><circle cx="12" cy="9" r="2.5"/><path d="M9 16h6"/></Ic>,
  sound: (p) => <Ic {...p}><path d="M11 5 6 9H3v6h3l5 4V5z"/><path d="M16 9a4 4 0 0 1 0 6"/></Ic>,
  sync: (p) => <Ic {...p}><path d="M21 12a9 9 0 1 1-3.5-7.1"/><path d="M21 4v5h-5"/></Ic>,
  search: (p) => <Ic {...p}><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></Ic>,
  plus: (p) => <Ic {...p}><path d="M12 5v14M5 12h14"/></Ic>,
  upload: (p) => <Ic {...p}><path d="M12 16V4M6 10l6-6 6 6"/><path d="M4 20h16"/></Ic>,
  yt: (p) => <Ic {...p}><rect x="2" y="6" width="20" height="12" rx="3"/><path d="m10 9 5 3-5 3z" fill="currentColor"/></Ic>,
  play: (p) => <Ic {...p}><path d="M7 5v14l12-7z" fill="currentColor"/></Ic>,
  pause: (p) => <Ic {...p}><path d="M7 5h3v14H7zM14 5h3v14h-3z" fill="currentColor"/></Ic>,
  prev: (p) => <Ic {...p}><path d="M19 5 8 12l11 7zM6 5v14"/></Ic>,
  next: (p) => <Ic {...p}><path d="M5 5l11 7-11 7zM18 5v14"/></Ic>,
  scissors: (p) => <Ic {...p}><circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M20 4 8.12 15.88M14.47 14.48 20 20M8.12 8.12 12 12"/></Ic>,
  trash: (p) => <Ic {...p}><path d="M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M6 6l1 14a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-14"/></Ic>,
  edit: (p) => <Ic {...p}><path d="M14 2 4 12v6h6L20 8z"/></Ic>,
  link: (p) => <Ic {...p}><path d="M10 13a5 5 0 0 0 7.07 0l3-3a5 5 0 0 0-7.07-7.07l-1.5 1.5"/><path d="M14 11a5 5 0 0 0-7.07 0l-3 3a5 5 0 0 0 7.07 7.07l1.5-1.5"/></Ic>,
  unlink: (p) => <Ic {...p}><path d="M9 17H5a3 3 0 0 1 0-6h4M15 7h4a3 3 0 0 1 0 6h-4M8 12h8M2 2l20 20"/></Ic>,
  more: (p) => <Ic {...p}><circle cx="12" cy="6" r="1.4" fill="currentColor"/><circle cx="12" cy="12" r="1.4" fill="currentColor"/><circle cx="12" cy="18" r="1.4" fill="currentColor"/></Ic>,
  filter: (p) => <Ic {...p}><path d="M3 5h18M6 12h12M10 19h4"/></Ic>,
  check: (p) => <Ic {...p}><path d="m5 12 5 5 9-11"/></Ic>,
  x: (p) => <Ic {...p}><path d="M6 6l12 12M18 6 6 18"/></Ic>,
  alert: (p) => <Ic {...p}><path d="M12 3 2 20h20z"/><path d="M12 9v5M12 17v.5"/></Ic>,
  info: (p) => <Ic {...p}><circle cx="12" cy="12" r="9"/><path d="M12 8v.5M12 11v5"/></Ic>,
  wifi: (p) => <Ic {...p}><path d="M2 9a16 16 0 0 1 20 0"/><path d="M5 13a11 11 0 0 1 14 0"/><path d="M8.5 16.5a6 6 0 0 1 7 0"/><circle cx="12" cy="20" r="0.6" fill="currentColor"/></Ic>,
  battery: (p) => <Ic {...p}><rect x="2" y="7" width="18" height="10" rx="2"/><path d="M22 11v2"/><rect x="4" y="9" width="11" height="6" rx="1" fill="currentColor" stroke="none"/></Ic>,
  sd: (p) => <Ic {...p}><path d="M14 2H8L4 6v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V6z"/><path d="M9 6V3M12 6V3M15 6V3"/></Ic>,
  power: (p) => <Ic {...p}><path d="M12 3v9"/><path d="M5.5 7.5a8 8 0 1 0 13 0"/></Ic>,
  cog: (p) => <Ic {...p}><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1Z"/></Ic>,
  download: (p) => <Ic {...p}><path d="M12 4v12M6 10l6 6 6-6"/><path d="M4 20h16"/></Ic>,
  reload: (p) => <Ic {...p}><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></Ic>,
  bell: (p) => <Ic {...p}><path d="M6 8a6 6 0 0 1 12 0c0 7 3 7 3 9H3c0-2 3-2 3-9z"/><path d="M10 21a2 2 0 0 0 4 0"/></Ic>,
  card: (p) => <Ic {...p}><rect x="3" y="5" width="18" height="14" rx="3"/><path d="M3 10h18"/><path d="M7 15h3"/></Ic>,
  figurine: (p) => <Ic {...p}><circle cx="12" cy="6" r="3"/><path d="M7 22v-7a5 5 0 0 1 10 0v7"/></Ic>,
  folder: (p) => <Ic {...p}><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></Ic>,
  file: (p) => <Ic {...p}><path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"/><path d="M14 3v6h6"/></Ic>,
  link2: (p) => <Ic {...p}><path d="M9 17H7a5 5 0 0 1 0-10h2"/><path d="M15 7h2a5 5 0 0 1 0 10h-2"/><path d="M8 12h8"/></Ic>,
  arrowRight: (p) => <Ic {...p}><path d="M5 12h14M13 5l7 7-7 7"/></Ic>,
  arrowUp: (p) => <Ic {...p}><path d="M12 19V5M5 12l7-7 7 7"/></Ic>,
  copy: (p) => <Ic {...p}><rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/></Ic>,
  spark: (p) => <Ic {...p}><path d="M12 3v4M12 17v4M3 12h4M17 12h4M5.6 5.6l2.8 2.8M15.6 15.6l2.8 2.8M5.6 18.4l2.8-2.8M15.6 8.4l2.8-2.8"/></Ic>,
  bell2: (p) => <Ic {...p}><path d="M6 8a6 6 0 0 1 12 0c0 7 3 7 3 9H3c0-2 3-2 3-9z"/></Ic>,
  power2: (p) => <Ic {...p}><path d="M12 3v9"/><path d="M5.5 7.5a8 8 0 1 0 13 0"/></Ic>,
  bolt: (p) => <Ic {...p}><path d="M13 2 4 14h7l-1 8 9-12h-7z" fill="currentColor"/></Ic>,
  volume: (p) => <Ic {...p}><path d="M11 5 6 9H3v6h3l5 4V5z"/></Ic>,
  volumeMute: (p) => <Ic {...p}><path d="M11 5 6 9H3v6h3l5 4V5z"/><path d="m17 9 4 6M21 9l-4 6"/></Ic>,
  startup: (p) => <Ic {...p}><path d="M5 12h14"/><path d="M13 5l7 7-7 7"/><circle cx="5" cy="12" r="1.5" fill="currentColor"/></Ic>,
  ready: (p) => <Ic {...p}><circle cx="12" cy="12" r="9"/><path d="m8 12 3 3 5-6"/></Ic>,
  list: (p) => <Ic {...p}><path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/></Ic>,
  grid: (p) => <Ic {...p}><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/></Ic>,
  sun: (p) => <Ic {...p}><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/></Ic>,
  moon: (p) => <Ic {...p}><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></Ic>,
  nfc: (p) => <Ic {...p}><rect x="2" y="5" width="13" height="14" rx="2.5"/><path d="M18 8a5 5 0 0 1 0 8"/><path d="M21 4.5a10 10 0 0 1 0 15"/></Ic>,
  signal: (p) => <Ic {...p}><path d="M3 18h2v3H3zM8 14h2v7H8zM13 9h2v12h-2zM18 4h2v17h-2z" fill="currentColor" stroke="none"/></Ic>,
  clock: (p) => <Ic {...p}><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></Ic>,
  pin: (p) => <Ic {...p}><path d="M12 22s7-7 7-12a7 7 0 0 0-14 0c0 5 7 12 7 12z"/><circle cx="12" cy="10" r="2.5"/></Ic>,
  warning: (p) => <Ic {...p}><path d="M12 3 2 20h20z"/><path d="M12 9v5"/><circle cx="12" cy="17.5" r="0.7" fill="currentColor"/></Ic>,
};

window.I = I;
