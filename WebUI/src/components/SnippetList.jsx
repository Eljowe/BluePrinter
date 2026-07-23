import { SnippetCard } from "./SnippetCard";

export function SnippetList({ snippets, playingSnippetId, playPositionSeconds }) {
  const ordered = [...snippets].sort((a, b) => b.id - a.id);

  return (
    <section className="snippet-list">
      <header className="section-header">
        <div className="section-title">
          <h2>Takes</h2>
          <span className="section-count">{snippets.length}</span>
        </div>
      </header>

      {snippets.length === 0 ? (
        <div className="snippet-empty">
          No takes yet. Hit the record button, play something, then stop.
        </div>
      ) : (
        <div className="snippet-grid">
          {ordered.map((s) => (
            <SnippetCard
              key={s.id}
              snippet={s}
              isPlaying={s.id === playingSnippetId}
              playPositionSeconds={playPositionSeconds}
            />
          ))}
        </div>
      )}
    </section>
  );
}
