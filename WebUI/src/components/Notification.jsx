import { useEffect } from "react";
import { IconX } from "./icons";

export function Notification({ notification, onDismiss }) {
  useEffect(() => {
    if (!notification) return undefined;
    const t = setTimeout(() => onDismiss?.(), 4500);
    return () => clearTimeout(t);
  }, [notification, onDismiss]);

  if (!notification) return null;

  const level = notification.level ?? "info";
  // Errors are announced assertively (they usually block the user's task);
  // everything else is polite. Clicking the toast still dismisses it.
  return (
    <div
      className={`notification notification-${level}`}
      role={level === "error" ? "alert" : "status"}
      aria-live={level === "error" ? "assertive" : "polite"}
      onClick={() => onDismiss?.()}
    >
      <div className="notification-message">{notification.message}</div>
      <button
        type="button"
        className="notification-close"
        onClick={() => onDismiss?.()}
        aria-label="Dismiss notification"
        title="Dismiss"
      >
        <IconX size={13} />
      </button>
    </div>
  );
}
