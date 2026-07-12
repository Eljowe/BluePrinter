import { useEffect } from "react";

export function Notification({ notification, onDismiss }) {
  useEffect(() => {
    if (!notification) return undefined;
    const t = setTimeout(() => onDismiss?.(), 4500);
    return () => clearTimeout(t);
  }, [notification, onDismiss]);

  if (!notification) return null;

  const level = notification.level ?? "info";
  return (
    <div className={`notification notification-${level}`} role="status" onClick={() => onDismiss?.()}>
      <div className="notification-message">{notification.message}</div>
    </div>
  );
}
