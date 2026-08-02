import { Component } from "react";

// Class error boundary: a render error in one panel must never blank
// the whole UI (white screen). Shows a small fallback instead.
export class ErrorBoundary extends Component {
  constructor(props) {
    super(props);
    this.state = { error: null };
  }

  static getDerivedStateFromError(error) {
    return { error };
  }

  componentDidCatch(error, info) {
    console.error("[BluePrinter] render error:", error, info);
  }

  render() {
    if (this.state.error) {
      return (
        <div className="error-boundary">
          <p className="error-boundary-title">Something went wrong in the UI</p>
          <p className="error-boundary-message">
            {String(this.state.error?.message ?? this.state.error)}
          </p>
          <button
            type="button"
            className="btn btn-sm"
            onClick={() => this.setState({ error: null })}
          >
            Try again
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}
