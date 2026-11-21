import { useEffect, useMemo, useRef, useState } from "react";
import { emit, FRONTEND_EVENTS } from "../bridge";
import { buildChordCatalog } from "../chords";
import { rootName, rootNames } from "../utils";
import {
  SECTION_BARS,
  SECTION_PRESETS,
  SONG_TEMPLATES,
  addSection,
  chartText,
  chordTemplates,
  clearChordAtBar,
  createSong,
  formatSongDuration,
  moveSection,
  removeSection,
  sectionStartBar,
  sectionTone,
  setChordAtBar,
  setSectionBars,
  snippetBarsAtSongTempo,
  snippetIdsInOrder,
  songDurationSeconds,
  suggestChords,
  totalBars,
  updateSection,
} from "../songModel";

const CHORD_ROWS = 8;
const NAME_DEBOUNCE_MS = 400;

function isEditable(element) {
  if (!element) return false;
  const tag = element.tagName;
  return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || element.isContentEditable;
}

function isControl(element) {
  return Boolean(element && element.closest
    && element.closest("button, a, [role='button'], [role='tab'], [role='switch'], [role='slider']"));
}

function keyLabel(song) {
  return song ? `${rootName(song.keyRoot, song.keyScale)} ${song.keyScale}` : "";
}

// Song builder (0065): a planner, not a timeline. Sections, chords, one
// linked recording per section, and the capture attach flow. All document
// edits are emitted through frontendSaveSong; the backend echo in the
// snippets payload reconciles the props.
export function Song({
  songs = [],
  snippets = [],
  transport = {},
  activeSongId = null,
  onActiveSongChange,
  onSwitchTab,
  pendingCapture = null,
  onPendingCaptureChange,
}) {
  const song = songs.find((entry) => entry.id === activeSongId) ?? songs[0] ?? null;
  const snippetMap = useMemo(
    () => new Map(snippets.map((snippet) => [Number(snippet.id), snippet])),
    [snippets],
  );

  const [selectedSectionId, setSelectedSectionId] = useState(null);
  const [nameDraft, setNameDraft] = useState(song?.name ?? "");
  const [sectionNameDraft, setSectionNameDraft] = useState("");
  const [ideaDraft, setIdeaDraft] = useState("");
  const [templateOpen, setTemplateOpen] = useState(false);
  const [suggestOpen, setSuggestOpen] = useState(false);
  const [chordPicker, setChordPicker] = useState(null); // { sectionId, bar }
  const [linkOpen, setLinkOpen] = useState(false);
  const [linkQuery, setLinkQuery] = useState("");
  const [pendingAttach, setPendingAttach] = useState(null);
  const [deleteConfirm, setDeleteConfirm] = useState(false);
  const [setlistName, setSetlistName] = useState("");
  const [copied, setCopied] = useState(false);

  const songRef = useRef(song);
  songRef.current = song;
  const nameTimer = useRef(null);
  const nameDraftRef = useRef(song?.name ?? "");

  const section = song?.sections.find((entry) => entry.id === selectedSectionId)
    ?? song?.sections[0]
    ?? null;
  const sectionId = section?.id ?? null;

  const capturing = Boolean(transport.recording || transport.preRollActive
    || transport.looperRecording || transport.looperPreRoll);
  const playingId = Number(transport.playingSnippetId ?? -1);
  const melodyPlayingId = Number(transport.melodyPlayingId ?? -1);
  const melodyPlayingSource = String(transport.melodyPlayingSource ?? "");
  const libraryFolder = String(transport.libraryFolder ?? "");

  const emitSave = (next) => {
    if (next) emit(FRONTEND_EVENTS.saveSong, { song: next });
  };

  // Commit the song-name draft now (debounce expiry, Enter, a song switch, or
  // unmount when another recording tab is opened). Blur alone deliberately
  // does not commit: a blur followed by a click elsewhere would emit a song
  // built from the pre-click section list and clobber that click's edit.
  const flushNameDraft = () => {
    if (nameTimer.current) {
      clearTimeout(nameTimer.current);
      nameTimer.current = null;
    }
    const latest = songRef.current;
    const trimmed = String(nameDraftRef.current ?? "").trim().slice(0, 80);
    if (!latest || !trimmed || trimmed === latest.name) return;
    emitSave({ ...latest, name: trimmed });
  };

  // Keep the selection valid as songs/sections change (create, delete,
  // reorder) without stealing a deliberate choice. A just-added section (or
  // a just-created song) is remembered in `preferSectionRef` until the
  // backend echo contains it, so the payload that predates the save can't
  // reset the selection to the first section.
  const preferSectionRef = useRef(null);
  useEffect(() => {
    if (!song) {
      if (selectedSectionId !== null) setSelectedSectionId(null);
      return;
    }

    const prefer = preferSectionRef.current;
    if (prefer) {
      if (prefer.songId !== song.id)
        return; // waiting for the new song payload
      if (!prefer.waitForSection || song.sections.some((entry) => entry.id === prefer.sectionId)) {
        preferSectionRef.current = null;
        const next = song.sections.some((entry) => entry.id === prefer.sectionId)
          ? prefer.sectionId
          : song.sections[0]?.id ?? null;
        if (next !== selectedSectionId) setSelectedSectionId(next);
        return;
      }
      return; // waiting for the new section payload
    }

    if (!song.sections.some((entry) => entry.id === selectedSectionId))
      setSelectedSectionId(song.sections[0]?.id ?? null);
  }, [song, selectedSectionId]);

  // Drafts follow the selected song/section.
  useEffect(() => {
    const name = song?.name ?? "";
    setNameDraft(name);
    nameDraftRef.current = name;
    clearTimeout(nameTimer.current);
    nameTimer.current = null;
  }, [song?.id]);

  useEffect(() => {
    setSectionNameDraft(section?.name ?? "");
    setIdeaDraft(section?.idea ?? "");
    setChordPicker(null);
    setSuggestOpen(false);
    setLinkOpen(false);
    setLinkQuery("");
  }, [song?.id, sectionId]);

  // Flush a name edit still inside the debounce window when the Song tab
  // unmounts (switching to Take/Loop) so the typed name is not lost.
  useEffect(() => () => {
    if (nameTimer.current) flushNameDraft();
  }, []);

  // Tempo sync, apply direction: once per active song id. The write-back
  // effect below is the other direction and never runs on the same tick.
  const appliedSongRef = useRef(null);
  const tempoSyncedRef = useRef(false);
  useEffect(() => {
    if (!song) {
      appliedSongRef.current = null;
      tempoSyncedRef.current = false;
      return;
    }
    if (appliedSongRef.current === song.id) return;
    appliedSongRef.current = song.id;
    tempoSyncedRef.current = false;

    const bpmMatches = Math.abs(Number(transport.bpm ?? 120) - Number(song.bpm)) <= 0.001;
    const meterMatches = Number(transport.timeSignatureNumerator ?? 4) === song.numerator
      && Number(transport.timeSignatureDenominator ?? 4) === song.denominator;

    if (!bpmMatches)
      emit(FRONTEND_EVENTS.setBpm, { bpm: song.bpm });
    if (!meterMatches) {
      emit(FRONTEND_EVENTS.setTimeSignature, {
        numerator: song.numerator,
        denominator: song.denominator,
      });
    }
    // Already in sync: arm the write direction immediately. Otherwise the
    // write-back effect arms on the transport push that matches the song.
    if (bpmMatches && meterMatches)
      tempoSyncedRef.current = true;
    // Only the song id is the trigger; the transport values are read at that
    // moment (a header edit afterwards is handled by the write-back below).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [song?.id]);

  // Tempo sync, write direction: while the Song tab is mounted (i.e. active),
  // a header BPM/meter change updates the song after a quiet 400 ms. The
  // first matching transport push after a song switch arms the direction, so
  // the apply above can never be mistaken for a user edit.
  useEffect(() => {
    const current = songRef.current;
    if (!current) return undefined;

    const bpm = Number(transport.bpm ?? 120);
    const numerator = Number(transport.timeSignatureNumerator ?? 4);
    const denominator = Number(transport.timeSignatureDenominator ?? 4);
    const matches = Math.abs(bpm - Number(current.bpm)) < 0.001
      && numerator === current.numerator
      && denominator === current.denominator;

    if (!tempoSyncedRef.current) {
      if (matches) tempoSyncedRef.current = true;
      return undefined;
    }
    if (matches) return undefined;

    const timer = setTimeout(() => {
      const latest = songRef.current;
      if (!latest) return;
      emitSave({ ...latest, bpm, numerator, denominator });
    }, 400);
    return () => clearTimeout(timer);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [transport.bpm, transport.timeSignatureNumerator, transport.timeSignatureDenominator, song?.id]);

  // Save & attach: the next snippets payload that contains an id we did not
  // have before is the freshly saved loop/take. Attach the highest new id,
  // record its estimated length at the song tempo, and give an autogenerated
  // name the song/section label.
  useEffect(() => {
    if (!pendingAttach) return;

    const previous = new Set(pendingAttach.previousIds);
    const fresh = snippets.filter((snippet) => !previous.has(Number(snippet.id)));
    if (fresh.length === 0) return;

    const newId = fresh.reduce((highest, snippet) => Math.max(highest, Number(snippet.id)), -1);
    const snippet = snippetMap.get(newId);
    const targetSong = songs.find((entry) => entry.id === pendingAttach.songId);
    const targetSection = targetSong?.sections.find((entry) => entry.id === pendingAttach.sectionId);

    if (!targetSong || !targetSection) {
      setPendingAttach(null);
      return;
    }

    const recordedBars = snippetBarsAtSongTempo(snippet, targetSong) || targetSection.bars;
    emitSave(updateSection(targetSong, targetSection.id, {
      snippetId: newId,
      recordedBpm: targetSong.bpm,
      recordedBars,
    }));

    const snippetName = String(snippet?.name ?? "");
    if (libraryFolder && snippet
        && (snippetName.startsWith("Snippet ") || snippetName.startsWith("Loop "))) {
      emit(FRONTEND_EVENTS.updateSnippet, {
        id: newId,
        name: `${targetSong.name} - ${targetSection.name}`,
        comments: snippet.comments ?? "",
      });
    }

    setPendingAttach(null);
    onPendingCaptureChange?.(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snippets, pendingAttach, libraryFolder]);

  // A pending attach that never sees a new snippet is abandoned after 2 s.
  useEffect(() => {
    if (!pendingAttach) return undefined;
    const timer = setTimeout(() => setPendingAttach(null), 2000);
    return () => clearTimeout(timer);
  }, [pendingAttach]);

  // Escape closes any open picker (chord, link, suggest, template).
  useEffect(() => {
    if (!templateOpen && !suggestOpen && !chordPicker && !linkOpen) return undefined;
    const onKeyDown = (event) => {
      if (event.key !== "Escape") return;
      event.preventDefault();
      setTemplateOpen(false);
      setSuggestOpen(false);
      setChordPicker(null);
      setLinkOpen(false);
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [templateOpen, suggestOpen, chordPicker, linkOpen]);

  const applySongTempo = (target) => {
    if (!target) return;
    if (Math.abs(Number(transport.bpm ?? 120) - Number(target.bpm)) > 0.001)
      emit(FRONTEND_EVENTS.setBpm, { bpm: target.bpm });
    if (Number(transport.timeSignatureNumerator ?? 4) !== target.numerator
        || Number(transport.timeSignatureDenominator ?? 4) !== target.denominator) {
      emit(FRONTEND_EVENTS.setTimeSignature, {
        numerator: target.numerator,
        denominator: target.denominator,
      });
    }
  };

  const recordSection = (targetSection, mode) => {
    if (!song || !targetSection || capturing) return;
    applySongTempo(song);

    if (mode === "loop") {
      // A section capture is always fresh. Overdub would layer onto an
      // existing loop and never auto-stop at the section length.
      if (transport.looperOverdub) emit(FRONTEND_EVENTS.setLooperOverdub, { enabled: false });
      emit(FRONTEND_EVENTS.setLooperLengthBars, { bars: targetSection.bars });
      onSwitchTab?.("loop");
      emit(FRONTEND_EVENTS.setLooperRecording, { enabled: true });
    } else {
      if (transport.takeOverdub) emit(FRONTEND_EVENTS.setTakeOverdub, { enabled: false });
      onSwitchTab?.("take");
      emit(FRONTEND_EVENTS.startRecording);
    }

    onPendingCaptureChange?.({
      songId: song.id,
      sectionId: targetSection.id,
      mode,
      startedAt: Date.now(),
    });
  };

  const stopSectionCapture = () => {
    if (!pendingCapture) return;
    if (pendingCapture.mode === "loop") emit(FRONTEND_EVENTS.setLooperRecording, { enabled: false });
    else emit(FRONTEND_EVENTS.stopRecording);
  };

  // Space belongs to the Song tab: the selected section is local state, so
  // the App handler deliberately ignores Space in this mode.
  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.defaultPrevented) return;
      if (event.key !== " ") return;
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      if (templateOpen || suggestOpen || chordPicker || linkOpen) return;
      if (isEditable(event.target) || isEditable(document.activeElement)) return;
      if (isControl(document.activeElement)) return;
      if (!section) return;
      event.preventDefault();

      if (capturing && pendingCapture?.sectionId === section.id) {
        stopSectionCapture();
        return;
      }
      recordSection(section, "loop");
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [section, capturing, pendingCapture, templateOpen, suggestOpen, chordPicker, linkOpen]);

  const handleNameChange = (value) => {
    setNameDraft(value);
    nameDraftRef.current = value;
    clearTimeout(nameTimer.current);
    nameTimer.current = setTimeout(() => {
      nameTimer.current = null;
      flushNameDraft();
    }, NAME_DEBOUNCE_MS);
  };

  const handleSongSelect = (id) => {
    flushNameDraft();
    setDeleteConfirm(false);
    onActiveSongChange?.(id);
  };

  const createFromTemplate = (templateId) => {
    // Commit a pending name edit for the current song before switching away.
    flushNameDraft();
    const template = SONG_TEMPLATES.find((entry) => entry.id === templateId);
    const next = createSong({
      name: templateId === "blank" ? "New song" : `${template?.name ?? "New"} song`,
      keyRoot: song?.keyRoot ?? 0,
      keyScale: song?.keyScale ?? "major",
      bpm: Number(transport.bpm) || 120,
      numerator: Number(transport.timeSignatureNumerator) || 4,
      denominator: Number(transport.timeSignatureDenominator) || 4,
      template: templateId,
    });
    emitSave(next);
    setTemplateOpen(false);
    setDeleteConfirm(false);
    onActiveSongChange?.(next.id);
    preferSectionRef.current = {
      songId: next.id,
      sectionId: next.sections[0]?.id ?? null,
      waitForSection: false,
    };
  };

  const addSectionToSong = () => {
    if (!song) return;
    const next = addSection(song);
    const added = next.sections[next.sections.length - 1];
    emitSave(next);
    preferSectionRef.current = {
      songId: song.id,
      sectionId: added?.id ?? null,
      waitForSection: true,
    };
  };

  const deleteSong = () => {
    if (!song) return;
    if (!deleteConfirm) {
      setDeleteConfirm(true);
      return;
    }
    emit(FRONTEND_EVENTS.deleteSong, { id: song.id });
    setDeleteConfirm(false);
    setPendingAttach(null);
    onPendingCaptureChange?.(null);
  };

  const linkSnippet = (snippet) => {
    if (!song || !section) return;
    const recordedBars = snippetBarsAtSongTempo(snippet, song) || section.bars;
    emitSave(updateSection(song, section.id, {
      snippetId: Number(snippet.id),
      recordedBpm: 0,
      recordedBars,
    }));
    setLinkOpen(false);
    setLinkQuery("");
  };

  const clearLink = () => {
    if (!song || !section) return;
    emitSave(updateSection(song, section.id, {
      snippetId: 0,
      recordedBpm: 0,
      recordedBars: 0,
    }));
  };

  const saveAndAttach = () => {
    if (!song || !section || !pendingCapture) return;
    const mode = pendingCapture.mode;
    if (mode === "loop" && !(Number(transport.audioLoopLength) > 0)) return;
    if (mode === "take" && !transport.takePending) return;

    setPendingAttach({
      songId: song.id,
      sectionId: section.id,
      previousIds: snippets.map((snippet) => Number(snippet.id)),
      mode,
    });
    if (mode === "loop") emit(FRONTEND_EVENTS.saveLoop);
    else emit(FRONTEND_EVENTS.saveTake, { id: transport.selectedTakeId });
  };

  const discardPendingTake = () => {
    emit(FRONTEND_EVENTS.discardTake, { id: transport.selectedTakeId });
    setPendingAttach(null);
    onPendingCaptureChange?.(null);
  };

  const createSetlistFromSong = () => {
    if (!song) return;
    const ids = snippetIdsInOrder(song);
    const name = setlistName.trim() || song.name;
    if (ids.length === 0 || !name) return;
    emit(FRONTEND_EVENTS.createSetlist, { name, ids });
    setSetlistName("");
  };

  const copyChart = async () => {
    if (!song || !navigator.clipboard?.writeText) return;
    try {
      await navigator.clipboard.writeText(chartText(song, snippets));
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      /* clipboard unavailable — ignore */
    }
  };

  const applyPreset = (preset) => {
    if (!song || !section) return;
    // Keep the input in step with the document, or a later blur would
    // commit the old name and revert the preset.
    setSectionNameDraft(preset.name);
    const named = updateSection(song, section.id, { name: preset.name });
    emitSave(setSectionBars(named, section.id, preset.bars));
  };

  const commitSectionName = () => {
    if (!song || !section) return;
    const trimmed = sectionNameDraft.trim().slice(0, 40);
    if (!trimmed || trimmed === section.name) {
      setSectionNameDraft(section.name);
      return;
    }
    emitSave(updateSection(song, section.id, { name: trimmed }));
  };

  const commitIdea = () => {
    if (!song || !section) return;
    const trimmed = ideaDraft.trim().slice(0, 200);
    if (trimmed === section.idea) return;
    emitSave(updateSection(song, section.id, { idea: trimmed }));
  };

  const catalog = useMemo(
    () => (song ? buildChordCatalog(song.keyRoot, song.keyScale) : []),
    [song?.keyRoot, song?.keyScale],
  );
  const diatonicChords = catalog.filter((chord) => chord.group === "diatonic");
  const borrowedChords = catalog.filter((chord) => chord.group === "borrowed");
  const templates = song ? chordTemplates(song.keyScale) : [];
  const linkedSnippet = section?.snippetId > 0 ? snippetMap.get(Number(section.snippetId)) : null;
  const missingLink = Boolean(section?.snippetId > 0 && !linkedSnippet);
  const tempoMismatch = Boolean(section && section.recordedBpm > 0
    && Math.abs(Number(section.recordedBpm) - Number(song?.bpm ?? 0)) > 0.5);
  const linkedPlaying = Boolean(linkedSnippet && playingId === Number(linkedSnippet.id));
  const linkedMelodyPlaying = Boolean(linkedSnippet
    && melodyPlayingSource === "snippet"
    && melodyPlayingId === Number(linkedSnippet.id));
  const linkedIds = song ? snippetIdsInOrder(song) : [];
  const pendingHere = Boolean(pendingCapture && song && pendingCapture.songId === song.id
    && section && pendingCapture.sectionId === section.id);
  const hasCaptureMedia = pendingHere && (
    (pendingCapture.mode === "loop" && Number(transport.audioLoopLength) > 0)
    || (pendingCapture.mode === "take" && Boolean(transport.takePending))
  );

  const linkOptions = useMemo(() => {
    const query = linkQuery.trim().toLowerCase();
    return snippets
      .filter((snippet) => !query
        || String(snippet.name ?? "").toLowerCase().includes(query)
        || String(snippet.key ?? "").toLowerCase().includes(query))
      .slice(0, 40);
  }, [snippets, linkQuery]);

  if (!song) {
    return (
      <div className="song-builder song-empty">
        <div className="song-empty-card">
          <h3>Plan a song</h3>
          <p>
            A song holds ordered sections, one chord per bar, a melody/idea line and one linked
            recording per section. Start from a template:
          </p>
          <div className="song-empty-templates">
            {SONG_TEMPLATES.map((template) => (
              <button
                key={template.id}
                type="button"
                className="btn song-template-btn"
                onClick={() => createFromTemplate(template.id)}
              >
                <strong>{template.name}</strong>
                <span>{template.description}</span>
              </button>
            ))}
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="song-builder">
      <div className="song-header">
        <label className="song-field">
          <span>Song</span>
          <select
            value={song.id}
            onChange={(event) => handleSongSelect(event.target.value)}
            aria-label="Active song"
          >
            {songs.map((entry) => (
              <option key={entry.id} value={entry.id}>{entry.name}</option>
            ))}
          </select>
        </label>

        <div className="song-new">
          <button
            type="button"
            className="btn btn-sm"
            onClick={() => setTemplateOpen((open) => !open)}
            aria-expanded={templateOpen}
            aria-haspopup="true"
          >
            New
          </button>
          {templateOpen ? (
            <div className="song-template-popover" role="group" aria-label="New song template">
              {SONG_TEMPLATES.map((template) => (
                <button
                  key={template.id}
                  type="button"
                  className="song-template-option"
                  onClick={() => createFromTemplate(template.id)}
                >
                  <strong>{template.name}</strong>
                  <span>{template.description}</span>
                </button>
              ))}
            </div>
          ) : null}
        </div>

        <label className="song-field song-name-field">
          <span>Name</span>
          <input
            type="text"
            value={nameDraft}
            maxLength={80}
            onChange={(event) => handleNameChange(event.target.value)}
            onKeyDown={(event) => {
              if (event.key !== "Enter") return;
              event.preventDefault();
              flushNameDraft();
            }}
            onBlur={() => {
              if (!nameDraft.trim()) {
                setNameDraft(song.name);
                nameDraftRef.current = song.name;
              }
            }}
            aria-label="Song name"
          />
        </label>

        <label className="song-field">
          <span>Key</span>
          <select
            value={song.keyRoot}
            onChange={(event) => emitSave({ ...song, keyRoot: Number(event.target.value) })}
            aria-label="Song key root"
            title="Changing the key does not transpose existing chords."
          >
            {rootNames(song.keyScale).map((name, index) => (
              <option key={name} value={index}>{name}</option>
            ))}
          </select>
        </label>

        <label className="song-field">
          <span>Scale</span>
          <select
            value={song.keyScale}
            onChange={(event) => emitSave({ ...song, keyScale: event.target.value })}
            aria-label="Song scale"
          >
            <option value="major">major</option>
            <option value="minor">minor</option>
          </select>
        </label>

        <span className="song-summary">
          {keyLabel(song)} · {totalBars(song)} bars - approx {formatSongDuration(songDurationSeconds(song))}
        </span>

        <button type="button" className="btn btn-sm" onClick={copyChart}>
          {copied ? "Copied" : "Copy chart"}
        </button>
      </div>

      <div className="song-arrangement" role="group" aria-label="Song arrangement">
        {song.sections.map((entry) => (
          <button
            key={entry.id}
            type="button"
            className={`song-block song-tone-${sectionTone(entry.name)} ${entry.id === sectionId ? "is-selected" : ""}`}
            style={{ flexGrow: entry.bars }}
            onClick={() => setSelectedSectionId(entry.id)}
            aria-pressed={entry.id === sectionId}
            aria-label={`${entry.name}, ${entry.bars} bars`}
          >
            <span className="song-block-name">{entry.name}</span>
            <span className="song-block-bars">{entry.bars}</span>
          </button>
        ))}
        {song.sections.length === 0 ? (
          <span className="song-hint">No sections yet — add one below.</span>
        ) : null}
      </div>

      <div className="song-columns">
        <div className="song-sections" aria-label="Sections">
          {song.sections.map((entry, index) => {
            const snippet = entry.snippetId > 0 ? snippetMap.get(Number(entry.snippetId)) : null;
            const summary = entry.chords.slice(0, 3).map((chord) => chord.symbol).join(" · ");
            return (
              <div
                key={entry.id}
                className={`song-row ${entry.id === sectionId ? "is-selected" : ""}`}
              >
                <button
                  type="button"
                  className="song-row-main"
                  onClick={() => setSelectedSectionId(entry.id)}
                  aria-pressed={entry.id === sectionId}
                >
                  <span className="song-row-index">{sectionStartBar(song, index) + 1}</span>
                  <span className="song-row-text">
                    <span className="song-row-name">{entry.name}</span>
                    <span className="song-row-meta">
                      {entry.bars} bars{summary ? ` · ${summary}` : ""}
                      {entry.snippetId > 0 ? (snippet ? " · LINKED" : " · MISSING") : ""}
                    </span>
                  </span>
                  {pendingCapture?.songId === song.id && pendingCapture.sectionId === entry.id ? (
                    <span className="song-row-badge" aria-label="Capture not attached">REC</span>
                  ) : null}
                </button>
                <div className="song-row-move">
                  <button
                    type="button"
                    className="song-icon-btn"
                    onClick={() => emitSave(moveSection(song, entry.id, -1))}
                    disabled={index === 0}
                    aria-label={`Move ${entry.name} up`}
                  >
                    ↑
                  </button>
                  <button
                    type="button"
                    className="song-icon-btn"
                    onClick={() => emitSave(moveSection(song, entry.id, 1))}
                    disabled={index === song.sections.length - 1}
                    aria-label={`Move ${entry.name} down`}
                  >
                    ↓
                  </button>
                  <button
                    type="button"
                    className="song-icon-btn"
                    onClick={() => emitSave(removeSection(song, entry.id))}
                    aria-label={`Remove ${entry.name}`}
                  >
                    ×
                  </button>
                </div>
              </div>
            );
          })}
          <button type="button" className="btn btn-sm song-add-section" onClick={addSectionToSong}>
            Add section
          </button>
        </div>

        <div className="song-editor">
          {section ? (
            <>
              <div className="song-editor-head">
                <label className="song-field">
                  <span>Section</span>
                  <input
                    type="text"
                    value={sectionNameDraft}
                    maxLength={40}
                    onChange={(event) => setSectionNameDraft(event.target.value)}
                    onBlur={commitSectionName}
                    onKeyDown={(event) => { if (event.key === "Enter") commitSectionName(); }}
                    aria-label="Section name"
                  />
                </label>
                <label className="song-field">
                  <span>Bars</span>
                  <select
                    value={section.bars}
                    onChange={(event) => emitSave(setSectionBars(song, section.id, Number(event.target.value)))}
                    aria-label="Section bars"
                  >
                    {SECTION_BARS.map((bars) => (
                      <option key={bars} value={bars}>{bars}</option>
                    ))}
                  </select>
                </label>
              </div>

              <div className="song-presets" role="group" aria-label="Section presets">
                {SECTION_PRESETS.map((preset) => (
                  <button
                    key={preset.id}
                    type="button"
                    className="song-chip"
                    onClick={() => applyPreset(preset)}
                  >
                    {preset.name}
                  </button>
                ))}
              </div>

              <div className="song-chord-head">
                <span className="song-subhead">Chords</span>
                <button
                  type="button"
                  className="btn btn-sm"
                  onClick={() => setSuggestOpen((open) => !open)}
                  aria-expanded={suggestOpen}
                >
                  Suggest
                </button>
                {suggestOpen ? (
                  <div className="song-suggest" role="group" aria-label="Chord suggestions">
                    {templates.map((template) => (
                      <button
                        key={template.id}
                        type="button"
                        className="song-chip"
                        onClick={() => {
                          emitSave(suggestChords(song, section.id, template.id));
                          setSuggestOpen(false);
                        }}
                      >
                        {template.name}
                      </button>
                    ))}
                  </div>
                ) : null}
              </div>

              <div
                className="song-chord-grid"
                role="group"
                aria-label={`Chords, one cell per bar (${section.bars} bars)`}
                style={{ gridTemplateColumns: `repeat(${Math.min(CHORD_ROWS, section.bars)}, minmax(0, 1fr))` }}
              >
                {Array.from({ length: section.bars }, (_, bar) => {
                  const event = section.chords.find((chord) => chord.bar === bar);
                  const held = !event && section.chords.some((chord) => chord.bar < bar);
                  return (
                    <button
                      key={bar}
                      type="button"
                      className={`song-chord-cell ${event ? "has-chord" : held ? "is-held" : ""}`}
                      onClick={() => setChordPicker({ sectionId: section.id, bar })}
                      aria-label={`Bar ${bar + 1}${event ? `: ${event.symbol}` : held ? ", chord held" : ", no chord"}`}
                      aria-haspopup="true"
                    >
                      {event ? event.symbol : held ? "·" : "+"}
                    </button>
                  );
                })}
              </div>

              {chordPicker && chordPicker.sectionId === section.id ? (
                <div className="song-chord-picker" role="dialog" aria-label={`Chord for bar ${chordPicker.bar + 1}`}>
                  <div className="song-picker-head">
                    <span>Bar {chordPicker.bar + 1}</span>
                    <button type="button" className="song-icon-btn" onClick={() => setChordPicker(null)} aria-label="Close chord picker">×</button>
                  </div>
                  <div className="song-chord-group">
                    <span className="song-subhead">Diatonic</span>
                    <div className="song-chord-options">
                      {diatonicChords.map((chord) => (
                        <button
                          key={`d-${chord.id}`}
                          type="button"
                          className="song-chord-option"
                          onClick={() => {
                            emitSave(setChordAtBar(song, section.id, chordPicker.bar, chord));
                            setChordPicker(null);
                          }}
                        >
                          <span className="song-chord-roman">{chord.roman}</span>
                          <span>{chord.symbol}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                  <div className="song-chord-group">
                    <span className="song-subhead">Borrowed</span>
                    <div className="song-chord-options">
                      {borrowedChords.map((chord) => (
                        <button
                          key={`b-${chord.id}`}
                          type="button"
                          className="song-chord-option"
                          title={chord.label}
                          onClick={() => {
                            emitSave(setChordAtBar(song, section.id, chordPicker.bar, chord));
                            setChordPicker(null);
                          }}
                        >
                          <span className="song-chord-roman">{chord.roman}</span>
                          <span>{chord.symbol}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                  <button
                    type="button"
                    className="btn btn-sm"
                    onClick={() => {
                      emitSave(clearChordAtBar(song, section.id, chordPicker.bar));
                      setChordPicker(null);
                    }}
                  >
                    Clear bar
                  </button>
                </div>
              ) : null}

              <label className="song-field song-idea">
                <span>Melody / idea</span>
                <input
                  type="text"
                  value={ideaDraft}
                  maxLength={200}
                  onChange={(event) => setIdeaDraft(event.target.value)}
                  onBlur={commitIdea}
                  onKeyDown={(event) => { if (event.key === "Enter") commitIdea(); }}
                  aria-label="Section melody or idea"
                />
              </label>

              <div className="song-recording">
                <div className="song-recording-head">
                  <span className="song-subhead">Recording</span>
                  {linkedSnippet ? (
                    <span className={`song-link-badge ${linkedPlaying ? "is-playing" : ""}`}>
                      {linkedPlaying ? "PLAYING" : "LINKED"}
                    </span>
                  ) : null}
                </div>

                {linkedSnippet ? (
                  <div className="song-recording-card">
                    <div className="song-recording-title">
                      <strong>{linkedSnippet.name}</strong>
                      <span>
                        {linkedSnippet.key ? `${linkedSnippet.key} · ` : ""}
                        {Number(linkedSnippet.durationSeconds ?? 0).toFixed(1)} s
                        {section.recordedBpm > 0 ? (
                          <span className={tempoMismatch ? "song-tempo-warn" : undefined}>
                            {` · ${Math.round(section.recordedBpm)} BPM`
                              + (tempoMismatch ? ` (song ${Math.round(song.bpm)})` : "")}
                          </span>
                        ) : null}
                        {section.recordedBars > 0 ? ` · ${section.recordedBars} bars` : ""}
                      </span>
                    </div>
                    <div className="song-recording-actions">
                      <button
                        type="button"
                        className="btn btn-sm"
                        onClick={() => emit(
                          linkedPlaying ? FRONTEND_EVENTS.stopPlayback : FRONTEND_EVENTS.startPlayback,
                          linkedPlaying ? undefined : { id: Number(linkedSnippet.id) },
                        )}
                      >
                        {linkedPlaying ? "Stop" : "Play"}
                      </button>
                      {Number(linkedSnippet.melodyCount ?? 0) > 0 ? (
                        <button
                          type="button"
                          className="btn btn-sm"
                          onClick={() => emit(FRONTEND_EVENTS.setSnippetMelodyPlayback, {
                            id: Number(linkedSnippet.id),
                            enabled: !linkedMelodyPlaying,
                          })}
                        >
                          {linkedMelodyPlaying ? "Stop melody" : `Play melody (${linkedSnippet.melodyCount})`}
                        </button>
                      ) : (
                        <span className="song-hint">No melody notes</span>
                      )}
                      <button type="button" className="btn btn-sm" onClick={clearLink}>
                        Clear link
                      </button>
                    </div>
                  </div>
                ) : (
                  <p className="song-hint">
                    {missingLink
                      ? "The linked recording is missing (deleted or moved). Clear the link and pick another."
                      : "No recording linked yet."}
                  </p>
                )}

                <div className="song-recording-actions">
                  <button
                    type="button"
                    className="btn btn-sm"
                    onClick={() => recordSection(section, "loop")}
                    disabled={capturing}
                    title={`Capture a ${section.bars}-bar loop at ${Math.round(song.bpm)} BPM`}
                  >
                    Record loop
                  </button>
                  <button
                    type="button"
                    className="btn btn-sm"
                    onClick={() => recordSection(section, "take")}
                    disabled={capturing}
                  >
                    Record take
                  </button>
                  <button
                    type="button"
                    className="btn btn-sm"
                    onClick={() => setLinkOpen((open) => !open)}
                    aria-expanded={linkOpen}
                  >
                    Link
                  </button>
                  {hasCaptureMedia ? (
                    <button type="button" className="btn btn-sm song-attach" onClick={saveAndAttach}>
                      Save &amp; attach
                    </button>
                  ) : null}
                  {pendingHere && pendingCapture.mode === "take" && transport.takePending ? (
                    <button type="button" className="btn btn-sm" onClick={discardPendingTake}>
                      Discard take
                    </button>
                  ) : null}
                  {missingLink ? (
                    <button type="button" className="btn btn-sm" onClick={clearLink}>
                      Clear link
                    </button>
                  ) : null}
                </div>

                {pendingHere && capturing && !hasCaptureMedia ? (
                  <p className="song-hint">
                    {pendingCapture.mode === "loop"
                      ? "Recording… switch back to the Loop tab to finish the capture, then Save & attach."
                      : "Recording… switch back to the Take tab to finish the take, then Save & attach."}
                  </p>
                ) : null}

                {linkOpen ? (
                  <div className="song-link-picker" role="dialog" aria-label="Link a library recording">
                    <div className="song-picker-head">
                      <span>Link a recording</span>
                      <button type="button" className="song-icon-btn" onClick={() => setLinkOpen(false)} aria-label="Close link picker">×</button>
                    </div>
                    <input
                      type="search"
                      value={linkQuery}
                      onChange={(event) => setLinkQuery(event.target.value)}
                      placeholder="Search name or key"
                      aria-label="Search recordings"
                      autoFocus
                    />
                    <div className="song-link-list">
                      {linkOptions.length === 0 ? (
                        <p className="song-hint">No matching recordings.</p>
                      ) : linkOptions.map((snippet) => (
                        <button
                          key={snippet.id}
                          type="button"
                          className="song-link-option"
                          onClick={() => linkSnippet(snippet)}
                        >
                          <span>{snippet.name}</span>
                          <span className="song-link-meta">
                            {snippet.key ? `${snippet.key} · ` : ""}
                            {Number(snippet.durationSeconds ?? 0).toFixed(1)} s
                          </span>
                        </button>
                      ))}
                    </div>
                  </div>
                ) : null}
              </div>
            </>
          ) : (
            <p className="song-hint">Add a section to start planning.</p>
          )}
        </div>
      </div>

      <div className="song-footer">
        <div className="song-setlist-form">
          <label className="song-field">
            <span>Create setlist</span>
            <input
              type="text"
              value={setlistName}
              maxLength={60}
              placeholder={song.name}
              onChange={(event) => setSetlistName(event.target.value)}
              aria-label="Setlist name"
            />
          </label>
          <button
            type="button"
            className="btn btn-sm"
            onClick={createSetlistFromSong}
            disabled={linkedIds.length === 0}
            title={linkedIds.length === 0
              ? "Link at least one recording first"
              : `${linkedIds.length} linked recording${linkedIds.length === 1 ? "" : "s"} in section order`}
          >
            Create setlist ({linkedIds.length})
          </button>
        </div>

        <button
          type="button"
          className={`btn btn-sm song-delete ${deleteConfirm ? "is-confirming" : ""}`}
          onClick={deleteSong}
        >
          {deleteConfirm ? "Confirm delete" : "Delete song"}
        </button>
      </div>

      {!libraryFolder ? (
        <p className="song-hint">
          No library folder is set: recordings and links are session-only until you choose one in
          the Library.
        </p>
      ) : null}
    </div>
  );
}
