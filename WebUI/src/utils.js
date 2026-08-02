// Utility helpers used across the UI.

// The 8 organisational colour tags a snippet can carry. `key` is the
// value stored in the backend / sidecar JSON; `main` is the strong
// accent and `soft` a pastel wash for backgrounds.
export const SNIPPET_COLORS = [
  { key: "red",    label: "Red",    main: "#e26d6a", soft: "#f6d3d1" },
  { key: "orange", label: "Orange", main: "#e89b4e", soft: "#f7e0c8" },
  { key: "yellow", label: "Yellow", main: "#d9b93c", soft: "#f7edc4" },
  { key: "green",  label: "Green",  main: "#6bbf6a", soft: "#d7ecd3" },
  { key: "teal",   label: "Teal",   main: "#4db6ac", soft: "#cdeae5" },
  { key: "blue",   label: "Blue",   main: "#4a90d9", soft: "#d3e4f6" },
  { key: "purple", label: "Purple", main: "#9575cd", soft: "#e2d9f2" },
  { key: "pink",   label: "Pink",   main: "#e87ea0", soft: "#f7d8e2" },
];

export function snippetColor(key) {
  return SNIPPET_COLORS.find((c) => c.key === key) ?? null;
}

export function formatTime(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "00:00.0";
  const total = Math.max(0, seconds);
  const mm = Math.floor(total / 60);
  const ss = Math.floor(total % 60);
  const tenths = Math.floor((total * 10) % 10);
  return `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}.${tenths}`;
}

export function formatDate(iso) {
  if (!iso) return "";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "";
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const dd = String(d.getDate()).padStart(2, "0");
  const hh = String(d.getHours()).padStart(2, "0");
  const min = String(d.getMinutes()).padStart(2, "0");
  return `${yyyy}-${mm}-${dd} ${hh}:${min}`;
}

export function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

export function debounce(fn, wait) {
  let timer = null;
  return (...args) => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => {
      timer = null;
      fn(...args);
    }, wait);
  };
}
