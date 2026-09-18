import { useEffect, useMemo, useState } from "react";
import { SnippetCard } from "./SnippetCard";
import { SNIPPET_COLORS, snippetColor } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconChevronDown, IconSearch, IconTag, IconTrash, IconX } from "./icons";

const SORT_OPTIONS = [
  { key: "newest", label: "Newest" },
  { key: "oldest", label: "Oldest" },
  { key: "name", label: "Name A–Z" },
  { key: "nameDesc", label: "Name Z–A" },
  { key: "longest", label: "Longest" },
  { key: "shortest", label: "Shortest" },
];

// How many takes render before the "Show more" button appears. The
// grid re-caps whenever the filters change so the first page always
// starts at the top of the result set.
const VISIBLE_PAGE = 10;
const SHOW_MORE_STEP = 10;

// The built-in colour labels stand in until the user renames a tag;
// a user name (from the properties file) wins.
function tagLabel(tagNames, colorKey) {
  const custom = tagNames?.[colorKey];
  if (typeof custom === "string" && custom.trim()) return custom.trim();
  return snippetColor(colorKey)?.label ?? colorKey;
}

// Popover with one editable name row per colour tag. Renames commit on
// blur/Enter; clearing a field resets the tag to its built-in label.
function TagRenamePopover({ tagNames, onRenameTag, onClose }) {
  const [draft, setDraft] = useState(() =>
    Object.fromEntries(SNIPPET_COLORS.map((c) => [c.key, tagNames?.[c.key] || ""])),
  );

  const commit = (key) => {
    const value = draft[key] ?? "";
    if (value.trim() !== (tagNames?.[key] || "")) onRenameTag(key, value);
  };

  return (
    <div className="snippet-tags-popover" role="dialog" aria-label="Rename tags">
      <div className="snippet-tags-popover-header">
        <span className="snippet-tags-popover-title">Tag names</span>
        <button type="button" className="icon-btn" onClick={onClose} title="Close" aria-label="Close">
          <IconX size={13} />
        </button>
      </div>
      <p className="snippet-tags-popover-hint">
        Names show on the filter chips and snippet cards. Clear a field to use the built-in name.
      </p>
      {SNIPPET_COLORS.map((c) => (
        <label key={c.key} className="snippet-tag-rename-row">
          <span className="snippet-color-dot" style={{ background: c.main }} aria-hidden="true" />
          <input
            type="text"
            maxLength={24}
            value={draft[c.key] ?? ""}
            placeholder={c.label}
            onChange={(e) => setDraft((prev) => ({ ...prev, [c.key]: e.target.value }))}
            onBlur={() => commit(c.key)}
            onKeyDown={(e) => {
              if (e.key === "Enter") e.currentTarget.blur();
              if (e.key === "Escape") onClose();
            }}
            aria-label={`Name for the ${c.label} tag`}
          />
        </label>
      ))}
    </div>
  );
}

// Popover to create, rename and delete setlists. Reordering happens in
// the setlist-filtered view (the up/down controls on each card).
function SetlistManagePopover({ setlists, snippets, onCreate, onRename, onDelete, onClose }) {
  const [newName, setNewName] = useState("");
  const [draft, setDraft] = useState(() => Object.fromEntries(setlists.map((s) => [s.id, s.name])));
  const [confirmId, setConfirmId] = useState(null);

  useEffect(() => {
    if (!confirmId) return undefined;
    const t = setTimeout(() => setConfirmId(null), 3500);
    return () => clearTimeout(t);
  }, [confirmId]);

  const submitNew = () => {
    const trimmed = newName.trim();
    if (!trimmed) return;
    onCreate?.(trimmed);
    setNewName("");
  };

  const commitRename = (id) => {
    const value = String(draft[id] ?? "").trim();
    const current = setlists.find((s) => s.id === id)?.name ?? "";
    if (value && value !== current) onRename?.(id, value);
    else if (!value) setDraft((prev) => ({ ...prev, [id]: current }));
  };

  return (
    <div className="snippet-tags-popover" role="dialog" aria-label="Manage setlists">
      <div className="snippet-tags-popover-header">
        <span className="snippet-tags-popover-title">Setlists</span>
        <button type="button" className="icon-btn" onClick={onClose} title="Close" aria-label="Close">
          <IconX size={13} />
        </button>
      </div>
      <p className="snippet-tags-popover-hint">
        Group takes for a performance. Open a snippet and use its Setlists row to add it.
      </p>

      <div className="snippet-tag-rename-row">
        <input
          type="text"
          maxLength={60}
          value={newName}
          placeholder="New setlist name"
          onChange={(e) => setNewName(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") submitNew();
            if (e.key === "Escape") onClose();
          }}
          aria-label="New setlist name"
        />
        <button type="button" className="btn btn-sm" onClick={submitNew} disabled={!newName.trim()}>
          Add
        </button>
      </div>

      {setlists.length === 0 ? (
        <p className="snippet-tags-popover-hint">No setlists yet.</p>
      ) : setlists.map((sl) => {
        const count = sl.ids.filter((id) => snippets.some((s) => s.id === id)).length;
        return (
          <div key={sl.id} className="snippet-tag-rename-row">
            <input
              type="text"
              maxLength={60}
              value={draft[sl.id] ?? sl.name}
              onChange={(e) => setDraft((prev) => ({ ...prev, [sl.id]: e.target.value }))}
              onBlur={() => commitRename(sl.id)}
              onKeyDown={(e) => {
                if (e.key === "Enter") e.currentTarget.blur();
                if (e.key === "Escape") onClose();
              }}
              aria-label={`Rename setlist ${sl.name}`}
            />
            <span className="snippet-setlist-count" title={`${count} take${count === 1 ? "" : "s"}`}>{count}</span>
            <button
              type="button"
              className={`btn btn-sm btn-danger ${confirmId === sl.id ? "is-armed" : ""}`}
              onClick={() => {
                if (confirmId === sl.id) {
                  onDelete?.(sl.id);
                  setConfirmId(null);
                } else {
                  setConfirmId(sl.id);
                }
              }}
              title={confirmId === sl.id ? "Click again to delete this setlist" : "Delete setlist"}
            >
              {confirmId === sl.id ? "Confirm?" : "Delete"}
            </button>
          </div>
        );
      })}
    </div>
  );
}

export function SnippetList({
  snippets,
  tagNames,
  setlists = [],
  onRenameTag,
  onCreateSetlist,
  onRenameSetlist,
  onDeleteSetlist,
  onToggleSnippetSetlist,
  onSetSetlistOrder,
  playingSnippetId,
  playPositionSeconds,
  melodyPlayingSource,
  melodyPlayingId,
  loopHasLoop = false,
  onLoadIntoLooper,
  folder,
}) {
  const [query, setQuery] = useState("");
  const [sortBy, setSortBy] = useState("newest");
  const [tagFilter, setTagFilter] = useState(() => new Set());
  const [keyFilter, setKeyFilter] = useState("");
  const [favouritesOnly, setFavouritesOnly] = useState(false);
  const [setlistFilter, setSetlistFilter] = useState("");
  const [tagsOpen, setTagsOpen] = useState(false);
  const [setlistsOpen, setSetlistsOpen] = useState(false);
  const [selectMode, setSelectMode] = useState(false);
  const [selectedIds, setSelectedIds] = useState(() => new Set());
  const [confirmBulkDelete, setConfirmBulkDelete] = useState(false);
  const [visibleCount, setVisibleCount] = useState(VISIBLE_PAGE);

  // Any filter/sort change restarts the visible window at the top.
  useEffect(() => {
    setVisibleCount(VISIBLE_PAGE);
  }, [query, sortBy, tagFilter, keyFilter, favouritesOnly, setlistFilter]);

  // Distinct detected keys present in the library (sorted), for the
  // key filter dropdown.
  const availableKeys = useMemo(() => {
    const keys = new Set();
    for (const s of snippets) {
      if (typeof s.key === "string" && s.key.length > 0) keys.add(s.key);
    }
    return [...keys].sort();
  }, [snippets]);

  const activeSetlist = useMemo(
    () => setlists.find((sl) => sl.id === setlistFilter) ?? null,
    [setlists, setlistFilter],
  );

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    const tagKeys = [...tagFilter];
    return snippets.filter((s) => {
      if (favouritesOnly && !s.favourite) return false;
      if (activeSetlist && !activeSetlist.ids.includes(s.id)) return false;
      if (tagKeys.length > 0) {
        const color = String(s.color ?? "");
        const matchesTag = color !== "" && tagKeys.includes(color);
        const matchesUntagged = tagKeys.includes("__none") && color === "";
        if (!matchesTag && !matchesUntagged) return false;
      }
      if (keyFilter === "__none") {
        if (typeof s.key === "string" && s.key.length > 0) return false;
      } else if (keyFilter && s.key !== keyFilter) {
        return false;
      }
      if (q) {
        const haystack = [
          s.name ?? "",
          s.comments ?? "",
          s.key ?? "",
          Array.isArray(s.notes) ? s.notes.join(" ") : "",
        ].join(" ").toLowerCase();
        if (!haystack.includes(q)) return false;
      }
      return true;
    });
  }, [snippets, query, tagFilter, keyFilter, favouritesOnly, activeSetlist]);

  const ordered = useMemo(() => {
    // Inside a setlist, the setlist's own order is authoritative and stable.
    if (activeSetlist) {
      const byId = new Map(filtered.map((s) => [s.id, s]));
      const inOrder = [];
      for (const id of activeSetlist.ids) {
        const s = byId.get(id);
        if (s) inOrder.push(s);
      }
      return inOrder;
    }

    const sorters = {
      newest: (a, b) => b.id - a.id,
      oldest: (a, b) => a.id - b.id,
      name: (a, b) => String(a.name ?? "").localeCompare(String(b.name ?? "")),
      nameDesc: (a, b) => String(b.name ?? "").localeCompare(String(a.name ?? "")),
      longest: (a, b) => (Number(b.durationSeconds) || 0) - (Number(a.durationSeconds) || 0),
      shortest: (a, b) => (Number(a.durationSeconds) || 0) - (Number(b.durationSeconds) || 0),
    };
    return [...filtered].sort(sorters[sortBy] ?? sorters.newest);
  }, [filtered, sortBy, activeSetlist]);

  const toggleTag = (key) => {
    setTagFilter((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  };

  const moveInSetlist = (setlist, snippetId, delta) => {
    if (!onSetSetlistOrder) return;
    const ids = [...setlist.ids];
    const i = ids.indexOf(snippetId);
    const j = i + delta;
    if (i < 0 || j < 0 || j >= ids.length) return;
    [ids[i], ids[j]] = [ids[j], ids[i]];
    onSetSetlistOrder(setlist.id, ids);
  };

  const isFiltering = tagFilter.size > 0 || keyFilter !== "" || query.trim() !== ""
    || favouritesOnly || setlistFilter !== "";
  const activeTagCount = tagFilter.size;
  const visibleTakes = ordered.slice(0, visibleCount);
  const hiddenCount = ordered.length - visibleTakes.length;

  // Bulk selection (0035). Selection is pruned when snippets disappear so a
  // stale id can never be acted on.
  useEffect(() => {
    setSelectedIds((prev) => {
      if (prev.size === 0) return prev;
      const existing = new Set(snippets.map((s) => s.id));
      let changed = false;
      const next = new Set();
      for (const id of prev) {
        if (existing.has(id)) next.add(id);
        else changed = true;
      }
      return changed ? next : prev;
    });
  }, [snippets]);

  useEffect(() => {
    if (!confirmBulkDelete) return undefined;
    const t = setTimeout(() => setConfirmBulkDelete(false), 3500);
    return () => clearTimeout(t);
  }, [confirmBulkDelete]);

  const toggleSelected = (id) => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const exitSelect = () => {
    setSelectMode(false);
    setSelectedIds(new Set());
    setConfirmBulkDelete(false);
  };

  const allVisibleSelected = visibleTakes.length > 0
    && visibleTakes.every((s) => selectedIds.has(s.id));

  const toggleSelectAllVisible = () => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (allVisibleSelected) visibleTakes.forEach((s) => next.delete(s.id));
      else visibleTakes.forEach((s) => next.add(s.id));
      return next;
    });
  };

  const bulkSetColor = (color) => {
    if (selectedIds.size > 0) emit(FRONTEND_EVENTS.setSnippetsColor, { ids: [...selectedIds], color });
  };

  const bulkAddToSetlist = (setlistId) => {
    if (setlistId && selectedIds.size > 0) {
      emit(FRONTEND_EVENTS.addSnippetsToSetlist, { setlistId, ids: [...selectedIds] });
    }
  };

  const bulkDelete = () => {
    if (selectedIds.size === 0) return;
    if (!confirmBulkDelete) {
      setConfirmBulkDelete(true);
      return;
    }
    emit(FRONTEND_EVENTS.deleteSnippets, { ids: [...selectedIds] });
    exitSelect();
  };

  return (
    <section className="snippet-list">
      <header className="section-header section-header--minor">
        <div className="section-title">
          <h2>Takes</h2>
          <span className="section-count">
            {isFiltering ? `${filtered.length} / ${snippets.length}` : snippets.length}
          </span>
        </div>
      </header>

      <div className="snippet-toolbar">
        <label className="snippet-search">
          <IconSearch size={13} />
          <input
            type="search"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Search name, notes, key…"
            aria-label="Search takes"
          />
        </label>

        <label className="snippet-toolbar-select" title="Sort order">
          <select value={sortBy} onChange={(e) => setSortBy(e.target.value)} aria-label="Sort takes">
            {SORT_OPTIONS.map((o) => (
              <option key={o.key} value={o.key}>{o.label}</option>
            ))}
          </select>
        </label>

        {setlists.length > 0 ? (
          <label className="snippet-toolbar-select" title="Show one setlist in its saved order">
            <select
              value={setlistFilter}
              onChange={(e) => setSetlistFilter(e.target.value)}
              aria-label="Filter by setlist"
            >
              <option value="">All takes</option>
              {setlists.map((sl) => (
                <option key={sl.id} value={sl.id}>{sl.name}</option>
              ))}
            </select>
          </label>
        ) : null}

        <label className="snippet-toolbar-select" title="Filter by detected key">
          <select value={keyFilter} onChange={(e) => setKeyFilter(e.target.value)} aria-label="Filter by key">
            <option value="">Any key</option>
            <option value="__none">No key detected</option>
            {availableKeys.map((k) => (
              <option key={k} value={k}>{k}</option>
            ))}
          </select>
        </label>

        <button
          type="button"
          className={`snippet-fav-filter ${favouritesOnly ? "is-active" : ""}`}
          onClick={() => setFavouritesOnly((v) => !v)}
          aria-pressed={favouritesOnly}
          title={favouritesOnly ? "Showing favourites only" : "Show only favourites"}
        >
          {favouritesOnly ? "★" : "☆"} Favourites
        </button>

        <div className="snippet-tags-control">
          <button
            type="button"
            className={`snippet-tags-btn ${tagsOpen ? "is-open" : ""}`}
            onClick={() => setTagsOpen((v) => !v)}
            aria-expanded={tagsOpen}
            aria-haspopup="dialog"
            title="Rename the colour tags"
          >
            <IconTag size={13} />
            Tags
            {activeTagCount > 0 ? <span className="snippet-tags-count">{activeTagCount}</span> : null}
          </button>
          {tagsOpen ? (
            <TagRenamePopover tagNames={tagNames} onRenameTag={onRenameTag} onClose={() => setTagsOpen(false)} />
          ) : null}
        </div>

        <div className="snippet-tags-control">
          <button
            type="button"
            className={`snippet-tags-btn ${setlistsOpen ? "is-open" : ""}`}
            onClick={() => setSetlistsOpen((v) => !v)}
            aria-expanded={setlistsOpen}
            aria-haspopup="dialog"
            title="Create, rename or delete setlists"
          >
            <IconTag size={13} />
            Setlists
            {setlists.length > 0 ? <span className="snippet-tags-count">{setlists.length}</span> : null}
          </button>
          {setlistsOpen ? (
            <SetlistManagePopover
              setlists={setlists}
              snippets={snippets}
              onCreate={onCreateSetlist}
              onRename={onRenameSetlist}
              onDelete={onDeleteSetlist}
              onClose={() => setSetlistsOpen(false)}
            />
          ) : null}
        </div>

        <button
          type="button"
          className={`snippet-fav-filter ${selectMode ? "is-active" : ""}`}
          onClick={() => (selectMode ? exitSelect() : setSelectMode(true))}
          aria-pressed={selectMode}
          title={selectMode ? "Exit selection" : "Select several takes for bulk actions"}
        >
          {selectMode ? "Done" : "Select"}
        </button>
      </div>

      <div className="snippet-tag-chips" role="group" aria-label="Filter by tag colour">
        {SNIPPET_COLORS.map((c) => (
          <button
            key={c.key}
            type="button"
            className={`snippet-tag-chip ${tagFilter.has(c.key) ? "is-active" : ""}`}
            onClick={() => toggleTag(c.key)}
            aria-pressed={tagFilter.has(c.key)}
            title={`Show only ${tagLabel(tagNames, c.key)} takes`}
          >
            <span className="snippet-color-dot" style={{ background: c.main }} aria-hidden="true" />
            {tagLabel(tagNames, c.key)}
          </button>
        ))}
        <button
          type="button"
          className={`snippet-tag-chip ${tagFilter.has("__none") ? "is-active" : ""}`}
          onClick={() => toggleTag("__none")}
          aria-pressed={tagFilter.has("__none")}
          title="Show only untagged takes"
        >
          <span className="snippet-color-dot snippet-color-dot-none" aria-hidden="true" />
          Untagged
        </button>
        {isFiltering ? (
          <button
            type="button"
            className="snippet-filter-clear"
            onClick={() => {
              setQuery("");
              setKeyFilter("");
              setTagFilter(new Set());
              setFavouritesOnly(false);
              setSetlistFilter("");
            }}
            title="Clear search, tag, key, setlist and favourite filters"
          >
            <IconX size={11} />
            Clear
          </button>
        ) : null}
      </div>

      {selectMode ? (
        <div className="snippet-bulk-bar" role="group" aria-label="Bulk actions">
          <span className="snippet-bulk-count">
            {selectedIds.size} selected
          </span>
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            onClick={toggleSelectAllVisible}
            title="Select or deselect every take currently shown"
          >
            {allVisibleSelected ? "Deselect all" : "Select all"}
          </button>

          <span className="snippet-bulk-divider" aria-hidden="true" />

          <span className="snippet-bulk-label">Tag</span>
          <div className="snippet-swatches" role="group" aria-label="Set colour on the selection">
            {SNIPPET_COLORS.map((c) => (
              <button
                key={c.key}
                type="button"
                className="snippet-swatch"
                style={{ background: c.main }}
                onClick={() => bulkSetColor(c.key)}
                disabled={selectedIds.size === 0}
                title={`Set ${tagLabel(tagNames, c.key)} on the selection`}
                aria-label={`Set ${c.label} colour on the selection`}
              />
            ))}
            <button
              type="button"
              className="snippet-swatch snippet-swatch-none"
              onClick={() => bulkSetColor("")}
              disabled={selectedIds.size === 0}
              title="Clear colour on the selection"
              aria-label="Clear colour on the selection"
            >
              <IconX size={10} />
            </button>
          </div>

          {setlists.length > 0 ? (
            <label className="snippet-bulk-select" title="Add the selection to a setlist">
              <select
                value=""
                disabled={selectedIds.size === 0}
                onChange={(e) => {
                  if (e.target.value) bulkAddToSetlist(e.target.value);
                  e.target.value = "";
                }}
                aria-label="Add selection to a setlist"
              >
                <option value="">Add to setlist…</option>
                {setlists.map((sl) => (
                  <option key={sl.id} value={sl.id}>{sl.name}</option>
                ))}
              </select>
            </label>
          ) : null}

          <button
            type="button"
            className={`btn btn-sm btn-danger ${confirmBulkDelete ? "is-armed" : ""}`}
            onClick={bulkDelete}
            disabled={selectedIds.size === 0}
            title={confirmBulkDelete
              ? "Click again to permanently delete the selected takes"
              : "Delete the selected takes"}
          >
            <IconTrash size={13} />
            {confirmBulkDelete ? "Confirm delete" : "Delete"}
          </button>
        </div>
      ) : null}

      {ordered.length === 0 ? (
        snippets.length === 0 ? (
          <div className="snippet-empty snippet-empty--first-run">
            <p className="snippet-empty-title">No takes yet</p>
            <ol className="snippet-empty-steps">
              <li><strong>Record</strong> — hit the record button, play something, then stop.</li>
              <li><strong>Review</strong> — replay the pending take and keep the good one.</li>
              <li><strong>Save</strong> — send it here with a name and notes.</li>
            </ol>
            {folder ? (
              <p className="snippet-empty-note">Saved takes land in your library folder.</p>
            ) : (
              <button
                type="button"
                className="btn btn-sm"
                onClick={() => emit(FRONTEND_EVENTS.chooseLibraryFolder)}
              >
                Choose a library folder…
              </button>
            )}
          </div>
        ) : (
          <div className="snippet-empty">No takes match the current filters.</div>
        )
      ) : (
        <>
          <div className="snippet-grid">
            {visibleTakes.map((s) => (
              <SnippetCard
                key={s.id}
                snippet={s}
                tagNames={tagNames}
                setlists={setlists}
                selectable={selectMode}
                selected={selectedIds.has(s.id)}
                onToggleSelect={() => toggleSelected(s.id)}
                onToggleSnippetSetlist={onToggleSnippetSetlist}
                reorderContext={activeSetlist && activeSetlist.ids.includes(s.id)
                  ? {
                      index: activeSetlist.ids.indexOf(s.id),
                      count: activeSetlist.ids.length,
                      onMove: (delta) => moveInSetlist(activeSetlist, s.id, delta),
                    }
                  : null}
                isPlaying={s.id === playingSnippetId}
                playPositionSeconds={playPositionSeconds}
                isMelodyPlaying={melodyPlayingSource === "snippet" && Number(melodyPlayingId) === s.id}
                loopHasLoop={loopHasLoop}
                onLoadIntoLooper={onLoadIntoLooper}
              />
            ))}
          </div>
          {hiddenCount > 0 ? (
            <div className="snippet-show-more">
              <button
                type="button"
                className="btn btn-ghost btn-sm"
                onClick={() => setVisibleCount((n) => n + SHOW_MORE_STEP)}
                title={`Show ${Math.min(SHOW_MORE_STEP, hiddenCount)} more takes`}
              >
                <IconChevronDown size={13} />
                Show {Math.min(SHOW_MORE_STEP, hiddenCount)} more
                <span className="snippet-show-more-count">
                  {visibleTakes.length} / {ordered.length}
                </span>
              </button>
            </div>
          ) : null}
        </>
      )}
    </section>
  );
}
