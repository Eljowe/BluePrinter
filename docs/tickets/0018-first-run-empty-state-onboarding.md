---
id: "0018"
title: "First-run empty states and onboarding hints"
status: done
blocked_by: []
---

# First-run empty states and onboarding hints

## Problem

A fresh install opens to an empty takes list and a default chain with no
explanation. A new user cannot tell what to do first, so the recording workflow
(record → review → save, and setting a library folder) is discoverable only by
guessing.

## Current behaviour

Empty lists render as blank space; the chain rail shows no guidance. There is no
orientation for a first-time user.

## Required change

1. Empty state for the takes/snippet list: a short explanation of
   Record → review the pending take → Save to library, plus a call to set a
   library folder.
2. Empty state for the chain rail when no plugins are loaded: "Add a VST3 to build
   your chain".
3. Optional one-time hints highlighting the Record button, the Take/Loop tab bar,
   and Save. Persist a "seen" flag so returning users are not nagged.
4. Keep the copy short and in the app's existing tonal register.

## Acceptance criteria

- A fresh profile shows actionable empty states in both the library and the chain
  rail.
- Dismissed hints do not reappear.
- Users with existing snippets/chains see no onboarding chrome.

## Docs

`AGENTS.md` (UI conventions) if the pattern is reusable.

## Files

`WebUI/src/components/SnippetList.jsx`, `WebUI/src/components/PluginChain.jsx`,
`WebUI/src/App.jsx`, `WebUI/src/styles.css`.

## Out of scope

Guided interactive tours; sample content that ships with the installer.
