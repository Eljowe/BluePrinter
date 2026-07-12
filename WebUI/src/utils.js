// Utility helpers used across the UI.

export function formatTime(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "00:00.0";
  const total = Math.max(0, seconds);
  const mm = Math.floor(total / 60);
  const ss = Math.floor(total % 60);
  const tenths = Math.floor((total * 10) % 10);
  return `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}.${tenths}`;
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
