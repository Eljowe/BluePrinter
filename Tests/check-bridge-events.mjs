#!/usr/bin/env node
// Parity check for bridge event names (ticket 0013).
//
// Every frontend/backend event name is declared twice: as a
// `static constexpr const char*` in Source/WebViewEditor.h and as a value
// in the FRONTEND_EVENTS / BACKEND_EVENTS maps in WebUI/src/bridge.js.
// Nothing links the two, so a one-sided rename compiles cleanly and fails
// silently at runtime. This script asserts the two sets are identical and
// prints the one-sided names on mismatch.
//
// Run directly (`node Tests/check-bridge-events.mjs`) or via CTest.

import { readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, "..");

function readHeaderEvents() {
  const text = readFileSync(join(repoRoot, "Source", "WebViewEditor.h"), "utf8");

  const frontend = new Set();
  const backend = new Set();
  const pattern =
    /static\s+constexpr\s+const\s+char\s*\*\s*(frontend|backend)[A-Za-z0-9_]*\s*=\s*"([^"]+)"/g;

  for (const match of text.matchAll(pattern))
    (match[1] === "frontend" ? frontend : backend).add(match[2]);

  return { frontend, backend };
}

async function readBridgeEvents() {
  const module = await import(pathToFileURL(join(repoRoot, "WebUI", "src", "bridge.js")).href);

  if (!module.FRONTEND_EVENTS || !module.BACKEND_EVENTS)
    throw new Error("bridge.js must export FRONTEND_EVENTS and BACKEND_EVENTS");

  return {
    frontend: new Set(Object.values(module.FRONTEND_EVENTS)),
    backend: new Set(Object.values(module.BACKEND_EVENTS)),
  };
}

function compare(which, headerSet, jsSet) {
  const onlyHeader = [...headerSet].filter((name) => !jsSet.has(name)).sort();
  const onlyJs = [...jsSet].filter((name) => !headerSet.has(name)).sort();

  if (onlyHeader.length === 0 && onlyJs.length === 0) {
    console.log(`ok   ${which}: ${headerSet.size} event names match`);
    return true;
  }

  console.error(`FAIL ${which}: WebViewEditor.h and bridge.js disagree`);
  for (const name of onlyHeader) console.error(`  only in WebViewEditor.h: ${name}`);
  for (const name of onlyJs) console.error(`  only in bridge.js:      ${name}`);
  return false;
}

const header = readHeaderEvents();

if (header.frontend.size === 0 || header.backend.size === 0) {
  console.error("FAIL: no event constants parsed from Source/WebViewEditor.h");
  process.exit(1);
}

const js = await readBridgeEvents();

const frontendOk = compare("frontend", header.frontend, js.frontend);
const backendOk = compare("backend", header.backend, js.backend);

process.exit(frontendOk && backendOk ? 0 : 1);
