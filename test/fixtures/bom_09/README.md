# BOM-09 Chrome reference fixtures

Captured from `Google Chrome 158.0.8127.88` on macOS arm64 using fresh temporary
headless profiles (`--headless=new`) with external DNS disabled and no user
gesture, permission grant, or external network access:

- `chrome.json`: loopback secure context (`http://127.0.0.1:<port>/reference`)
- `chrome-insecure.json`: non-loopback HTTP insecure context (`http://bom-fixture.invalid:<port>/reference`)
- `chrome-opaque.json`: opaque `about:blank` context
- `probe.js`: exact script hashed in `probe_sha256`
