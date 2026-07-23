import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconExternal, IconFolder, IconRefresh } from "./icons";

export function LibraryFolderRow({ folder, error }) {
  const trimmed = folder && folder.length > 0 ? folder : "No library folder set";
  return (
    <section className="library-folder">
      <div className="library-folder-info">
        <div className="library-folder-label">
          <IconFolder size={12} />
          Library folder
        </div>
        <div className={`library-folder-path ${folder ? "" : "is-empty"}`} title={trimmed}>
          {trimmed}
        </div>
        {error ? <div className="library-folder-error">Last save error: {error}</div> : null}
      </div>
      <div className="library-folder-actions">
        <button type="button" className="btn btn-sm" onClick={() => emit(FRONTEND_EVENTS.chooseLibraryFolder)}>
          Choose…
        </button>
        <button
          type="button"
          className="icon-btn"
          disabled={!folder}
          onClick={() => emit(FRONTEND_EVENTS.refreshLibrary)}
          title={folder ? "Reload recordings from the library folder" : "Pick a library folder first"}
          aria-label="Refresh library"
        >
          <IconRefresh size={14} />
        </button>
        <button
          type="button"
          className="icon-btn"
          disabled={!folder}
          onClick={() => emit(FRONTEND_EVENTS.openLibraryFolder)}
          title={folder ? "Open library folder" : "Pick a library folder first"}
          aria-label="Open library folder"
        >
          <IconExternal size={14} />
        </button>
      </div>
    </section>
  );
}
