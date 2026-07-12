import { SnippetCard } from "./SnippetCard";

export function SnippetList({ snippets, playingSnippetId, playPositionSeconds }) {
  const ordered = [...snippets].sort((a, b) => b.id - a.id);

  return (
    <section className="snippet-list">
      <div className="snippet-list-header">
        <h2>Snippets <span className="count">({snippets.length})</span></h2>
      </div>

      {snippets.length === 0 ? (
        <div className="snippet-empty">
          No snippets yet. Hit <strong>RECORD</strong>, play something, then <strong>STOP</strong>.
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
