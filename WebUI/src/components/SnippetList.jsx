import { useEffect, useMemo, useState } from "react";
import { SnippetCard } from "./SnippetCard";
import { SNIPPET_COLORS, snippetColor } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconChevronDown, IconSearch, IconTag, IconX } from "./icons";

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

export function SnippetList({ snippets, tagNames, onRenameTag, playingSnippetId, playPositionSeconds, folder }) {
  const [query, setQuery] = useState("");
  const [sortBy, setSortBy] = useState("newest");
  const [tagFilter, setTagFilter] = useState(() => new Set());
  const [keyFilter, setKeyFilter] = useState("");
  const [tagsOpen, setTagsOpen] = useState(false);
  const [visibleCount, setVisibleCount] = useState(VISIBLE_PAGE);

  // Any filter/sort change restarts the visible window at the top.
  useEffect(() => {
    setVisibleCount(VISIBLE_PAGE);
  }, [query, sortBy, tagFilter, keyFilter]);

  // Distinct detected keys present in the library (sorted), for the
  // key filter dropdown.
  const availableKeys = useMemo(() => {
    const keys = new Set();
    for (const s of snippets) {
      if (typeof s.key === "string" && s.key.length > 0) keys.add(s.key);
    }
    return [...keys].sort();
  }, [snippets]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    const tagKeys = [...tagFilter];
    return snippets.filter((s) => {
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
  }, [snippets, query, tagFilter, keyFilter]);

  const ordered = useMemo(() => {
    const sorters = {
      newest: (a, b) => b.id - a.id,
      oldest: (a, b) => a.id - b.id,
      name: (a, b) => String(a.name ?? "").localeCompare(String(b.name ?? "")),
      nameDesc: (a, b) => String(b.name ?? "").localeCompare(String(a.name ?? "")),
      longest: (a, b) => (Number(b.durationSeconds) || 0) - (Number(a.durationSeconds) || 0),
      shortest: (a, b) => (Number(a.durationSeconds) || 0) - (Number(b.durationSeconds) || 0),
    };
    return [...filtered].sort(sorters[sortBy] ?? sorters.newest);
  }, [filtered, sortBy]);

  const toggleTag = (key) => {
    setTagFilter((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  };

  const isFiltering = tagFilter.size > 0 || keyFilter !== "" || query.trim() !== "";
  const activeTagCount = tagFilter.size;
  const visibleTakes = ordered.slice(0, visibleCount);
  const hiddenCount = ordered.length - visibleTakes.length;

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

        <label className="snippet-toolbar-select" title="Filter by detected key">
          <select value={keyFilter} onChange={(e) => setKeyFilter(e.target.value)} aria-label="Filter by key">
            <option value="">Any key</option>
            <option value="__none">No key detected</option>
            {availableKeys.map((k) => (
              <option key={k} value={k}>{k}</option>
            ))}
          </select>
        </label>

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
            }}
            title="Clear search, tag and key filters"
          >
            <IconX size={11} />
            Clear
          </button>
        ) : null}
      </div>

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
                isPlaying={s.id === playingSnippetId}
                playPositionSeconds={playPositionSeconds}
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
