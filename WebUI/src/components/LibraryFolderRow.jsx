import { FRONTEND_EVENTS, emit } from "../bridge";

export function LibraryFolderRow({ folder, error }) {
  const trimmed = folder && folder.length > 0 ? folder : "(not set)";
  return (
    <section className="library-folder">
      <div className="library-folder-info">
        <div className="library-folder-label">Library folder</div>
        <div className={`library-folder-path ${folder ? "" : "is-empty"}`} title={trimmed}>
          {trimmed}
        </div>
        {error ? <div className="library-folder-error">Last save error: {error}</div> : null}
      </div>
      <div className="library-folder-actions">
        <button type="button" onClick={() => emit(FRONTEND_EVENTS.chooseLibraryFolder)}>
          Choose…
        </button>
        <button
          type="button"
          disabled={!folder}
          onClick={() => emit(FRONTEND_EVENTS.refreshLibrary)}
          title={folder ? "Reload recordings from the library folder" : "Pick a library folder first"}
        >
          Refresh
        </button>
        <button
          type="button"
          disabled={!folder}
          onClick={() => emit(FRONTEND_EVENTS.openLibraryFolder)}
          title={folder ? "Open library folder" : "Pick a library folder first"}
        >
          Open
        </button>
      </div>
    </section>
  );
}
