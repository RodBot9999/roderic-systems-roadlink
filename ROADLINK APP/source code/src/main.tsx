import { Component, StrictMode } from "react";
import type { ReactNode } from "react";
import { createRoot } from "react-dom/client";
import "leaflet/dist/leaflet.css";
import "./styles.css";
import App from "./App";

class AppErrorBoundary extends Component<{ children: ReactNode }, { message: string | null }> {
  state = { message: null };

  static getDerivedStateFromError(error: Error) {
    return { message: error.message || String(error) };
  }

  componentDidCatch(error: Error) {
    console.error("RoadLink Fleet could not render", error);
  }

  render() {
    if (this.state.message) {
      return (
        <main style={{ minHeight: "100vh", padding: "min(10vh, 80px) 24px", background: "#0a0e12", color: "#eaf1f4", fontFamily: "system-ui, sans-serif" }}>
          <div style={{ maxWidth: 720, margin: "0 auto", padding: 32, border: "1px solid #6b4b25", borderRadius: 16, background: "#14191c" }}>
            <h1 style={{ marginTop: 0 }}>RoadLink display error</h1>
            <p>The receiver can keep running, but this screen could not render. Your device and saved trip data have not been erased.</p>
            <pre style={{ whiteSpace: "pre-wrap", wordBreak: "break-word", color: "#ffbd59" }}>{this.state.message}</pre>
            <button onClick={() => window.location.reload()} style={{ padding: "10px 18px", border: 0, borderRadius: 8, background: "#ffbd59", color: "#14191c", fontWeight: 700, cursor: "pointer" }}>Reload dashboard</button>
          </div>
        </main>
      );
    }
    return this.props.children;
  }
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <AppErrorBoundary><App /></AppErrorBoundary>
  </StrictMode>,
);
