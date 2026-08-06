// Inline stroke icons (lucide-style). All render at `size` (default 16)
// and inherit color via currentColor, so they match surrounding text.

function Svg({ size = 16, children, strokeWidth = 1.8, ...rest }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...rest}
    >
      {children}
    </svg>
  );
}

export function IconPlay(props) {
  return (
    <Svg {...props}>
      <path d="M7 4.5v15l13-7.5z" fill="currentColor" stroke="none" />
    </Svg>
  );
}

export function IconStop(props) {
  return (
    <Svg {...props}>
      <rect x="6" y="6" width="12" height="12" rx="1.5" fill="currentColor" stroke="none" />
    </Svg>
  );
}

export function IconChevronDown(props) {
  return (
    <Svg {...props}>
      <path d="m6 9 6 6 6-6" />
    </Svg>
  );
}

export function IconChevronUp(props) {
  return (
    <Svg {...props}>
      <path d="m6 15 6-6 6 6" />
    </Svg>
  );
}

export function IconChevronRight(props) {
  return (
    <Svg {...props}>
      <path d="m9 6 6 6-6 6" />
    </Svg>
  );
}

export function IconArrowUp(props) {
  return (
    <Svg {...props}>
      <path d="M12 19V5" />
      <path d="m5 12 7-7 7 7" />
    </Svg>
  );
}

export function IconArrowDown(props) {
  return (
    <Svg {...props}>
      <path d="M12 5v14" />
      <path d="m19 12-7 7-7-7" />
    </Svg>
  );
}

export function IconGrip(props) {
  return (
    <Svg {...props} strokeWidth={2.4}>
      <path d="M9 5h.01M9 12h.01M9 19h.01M15 5h.01M15 12h.01M15 19h.01" />
    </Svg>
  );
}

export function IconFolder(props) {
  return (
    <Svg {...props}>
      <path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
    </Svg>
  );
}

export function IconRefresh(props) {
  return (
    <Svg {...props}>
      <path d="M21 12a9 9 0 1 1-2.64-6.36" />
      <path d="M21 3v6h-6" />
    </Svg>
  );
}

export function IconExternal(props) {
  return (
    <Svg {...props}>
      <path d="M14 4h6v6" />
      <path d="M20 4 11 13" />
      <path d="M19 14v5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2h5" />
    </Svg>
  );
}

export function IconX(props) {
  return (
    <Svg {...props}>
      <path d="M18 6 6 18" />
      <path d="m6 6 12 12" />
    </Svg>
  );
}

export function IconTrash(props) {
  return (
    <Svg {...props}>
      <path d="M3 6h18" />
      <path d="M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2" />
      <path d="M19 6v13a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
      <path d="M10 11v6M14 11v6" />
    </Svg>
  );
}

export function IconSave(props) {
  return (
    <Svg {...props}>
      <path d="M12 3v12" />
      <path d="m7 10 5 5 5-5" />
      <path d="M4 19h16" />
    </Svg>
  );
}

export function IconEdit(props) {
  return (
    <Svg {...props}>
      <path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3" />
      <path d="M2 14h4M10 8h4M18 16h4" />
    </Svg>
  );
}

export function IconPlus(props) {
  return (
    <Svg {...props}>
      <path d="M12 5v14M5 12h14" />
    </Svg>
  );
}

export function IconAnalyze(props) {
  return (
    <Svg {...props}>
      <path d="M2 12h4l3-8 4 16 3-8h6" />
    </Svg>
  );
}

export function IconMetronome(props) {
  return (
    <Svg {...props}>
      <path d="m6 20 3.2-16h5.6L18 20" />
      <path d="M9 13h6" />
      <path d="m12 10 5-6" />
      <circle cx="17" cy="4" r="1" fill="currentColor" />
    </Svg>
  );
}

export function IconSearch(props) {
  return (
    <Svg {...props}>
      <circle cx="11" cy="11" r="7" />
      <path d="m20 20-3.8-3.8" />
    </Svg>
  );
}

export function IconTag(props) {
  return (
    <Svg {...props}>
      <path d="M3 11V4a1 1 0 0 1 1-1h7l10 10-8 8z" />
      <circle cx="7.5" cy="7.5" r="1.4" fill="currentColor" stroke="none" />
    </Svg>
  );
}

export function IconScan(props) {
  return (
    <Svg {...props}>
      <path d="M3 8V5a2 2 0 0 1 2-2h3M16 3h3a2 2 0 0 1 2 2v3M21 16v3a2 2 0 0 1-2 2h-3M8 21H5a2 2 0 0 1-2-2v-3" />
      <path d="M4 12h16" />
    </Svg>
  );
}

export function IconMidi(props) {
  return (
    <Svg {...props}>
      <circle cx="12" cy="12" r="2" fill="currentColor" stroke="none" />
      <circle cx="6" cy="17" r="2" fill="currentColor" stroke="none" />
      <circle cx="18" cy="17" r="2" fill="currentColor" stroke="none" />
      <circle cx="6" cy="7" r="2" fill="currentColor" stroke="none" />
      <circle cx="18" cy="7" r="2" fill="currentColor" stroke="none" />
      <line x1="6" y1="7" x2="6" y2="17" />
      <line x1="18" y1="7" x2="18" y2="17" />
      <line x1="12" y1="7" x2="12" y2="10" />
    </Svg>
  );
}
