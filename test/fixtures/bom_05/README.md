# BOM-05 permission, lock, session, and media-capability browser reference

Captured on **2026-11-25** using fresh temporary headless profiles in **Google
Chrome 158.0.8127.88**, macOS arm64, over a local CDP pipe with external DNS
disabled and no user profile, device, permission grant, or virtual-time budget.

- `probe.js` -> `chrome.json` (secure loopback HTTP), `chrome-insecure.json`
  (insecure hostname mapped to loopback), and `chrome-opaque.json` (`about:blank`
  opaque origin): shapes, global exposure, prototype key order, argument
  conversion, and synchronous/microtask/task outcomes for `geolocation`,
  `clipboard`, `credentials`, `locks`, `wakeLock`, `keyboard`, `mediaSession`,
  and `mediaCapabilities`.
