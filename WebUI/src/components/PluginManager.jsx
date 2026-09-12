import { useMemo, useState } from "react";

function basename(path) {
  if (!path) return "";
  const parts = String(path).split(/[\\/]/);
  return parts[parts.length - 1] || String(path);
}

// Plugin manager (0040): a read-only, metadata-only view of the discovered
// VST3s with search, rescan (reuses the async folder scan) and per-entry
// quarantine control. Nothing here ever instantiates a plugin — it renders the
// cached scan result and the persisted quarantine list.
export function PluginManager({ plugins, quarantine, scanState, onRescan, onClearQuarantine }) {
  const [query, setQuery] = useState("");

  const list = useMemo(() => (Array.isArray(plugins) ? plugins : []), [plugins]);
  const quarantined = useMemo(
    () => new Set((Array.isArray(quarantine) ? quarantine : []).map((f) => String(f).toLowerCase())),
    [quarantine],
  );

  const { rows, orphans } = useMemo(() => {
    const scanned = new Set();
    const all = list.map((p) => {
      const file = basename(p.path || "");
      scanned.add(file.toLowerCase());
      return {
        name: p.name || file || "Unknown plugin",
        vendor: p.manufacturer || "",
        path: p.path || "",
        file,
        quarantined: quarantined.has(file.toLowerCase()),
      };
    });

    // Quarantine entries with no scanned file (uninstalled, or the path
    // changed) still need a way to be cleared.
    const extra = [];
    for (const f of (Array.isArray(quarantine) ? quarantine : [])) {
      if (!scanned.has(String(f).toLowerCase())) extra.push({ file: String(f) });
    }
    return { rows: all, orphans: extra };
  }, [list, quarantine, quarantined]);

  const q = query.trim().toLowerCase();
  const matches = (name, vendor, path) => !q
    || name.toLowerCase().includes(q)
    || vendor.toLowerCase().includes(q)
    || path.toLowerCase().includes(q);

  const shown = rows.filter((r) => matches(r.name, r.vendor, r.path));
  const shownOrphans = orphans.filter((o) => matches(o.file, "", ""));
  const quarantineCount = rows.filter((r) => r.quarantined).length + orphans.length;

  const scanning = Boolean(scanState?.active);
  const scanTotal = Number(scanState?.total ?? 0);
  const scanCurrent = Number(scanState?.current ?? 0);
  const scanFile = typeof scanState?.currentFile === "string" ? scanState.currentFile : "";

  return (
    <div className="plugins-panel" role="group" aria-label="Plugin manager">
      <div className="plugins-toolbar">
        <input
          type="text"
          className="plugins-search"
          value={query}
          placeholder="Search name, vendor or path"
          onChange={(e) => setQuery(e.target.value)}
          aria-label="Search plugins"
        />
        <button type="button" className="btn btn-sm" onClick={onRescan} disabled={scanning}>
          {scanning ? "Scanning…" : "Rescan"}
        </button>
      </div>

      {scanning ? (
        <p className="plugins-progress" aria-live="polite">
          {scanTotal > 0
            ? `Scanning ${scanCurrent} / ${scanTotal}${scanFile ? ` — ${scanFile}` : ""}`
            : "Scanning…"}
        </p>
      ) : null}

      <p className="plugins-count">
        {list.length} plugin{list.length === 1 ? "" : "s"}
        {q ? ` · ${shown.length + shownOrphans.length} shown` : ""}
        {quarantineCount > 0 ? ` · ${quarantineCount} quarantined` : ""}
      </p>

      {shown.length === 0 && shownOrphans.length === 0 ? (
        <p className="plugins-empty">
          {list.length === 0
            ? "No plugins scanned yet. Rescan the VST3 folder to discover them."
            : "No plugins match the search."}
        </p>
      ) : (
        <ul className="plugins-list">
          {shown.map((r, i) => (
            <li key={`${r.path || r.file}-${i}`} className={`plugins-item ${r.quarantined ? "is-quarantined" : ""}`}>
              <div className="plugins-item-info">
                <span className="plugins-item-name" title={r.path}>{r.name}</span>
                <span className="plugins-item-meta">
                  {r.vendor ? `${r.vendor} · ` : ""}{r.file}
                </span>
              </div>
              {r.quarantined ? (
                <>
                  <span className="plugins-badge" title="Skipped after a crash; re-add it from a chain to retry">
                    Quarantined
                  </span>
                  <button
                    type="button"
                    className="btn btn-sm"
                    onClick={() => onClearQuarantine(r.file)}
                    title="Clear the quarantine entry so the plugin can be retried"
                  >
                    Allow retry
                  </button>
                </>
              ) : null}
            </li>
          ))}

          {shownOrphans.map((o) => (
            <li key={`orphan-${o.file}`} className="plugins-item is-quarantined">
              <div className="plugins-item-info">
                <span className="plugins-item-name" title={o.file}>{o.file}</span>
                <span className="plugins-item-meta">not in the scanned folder</span>
              </div>
              <span className="plugins-badge" title="Skipped after a crash; re-add it from a chain to retry">
                Quarantined
              </span>
              <button
                type="button"
                className="btn btn-sm"
                onClick={() => onClearQuarantine(o.file)}
                title="Clear the quarantine entry so the plugin can be retried"
              >
                Allow retry
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
