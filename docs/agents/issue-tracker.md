# Issue tracker: Local Markdown

Issues and specs for this repo live as markdown files under `docs/tickets/`, versioned
with the code. This repo does **not** use GitHub Issues; there is no `gh` dependency.
The maintainer commits ticket changes alongside the code.

## Layout

- One file per ticket: `docs/tickets/NNNN-<slug>.md`, numbered from `0001` in creation
  order. Slugs are kebab-case and stay stable once created.
- The index is `docs/tickets/README.md` — a table of ticket / title / status / blockers.
  Update it whenever a ticket is added or a status changes.
- A spec and its implementation tickets are a single sequence: the spec is the first
  ticket, and implementation tickets reference it by id in the body.

## Ticket file format

Each ticket starts with YAML frontmatter, then free-form sections:

```markdown
---
id: "0001"
title: "Rework the Monitor section: ..."
status: ready-for-agent
blocked_by: []
---

# Rework the Monitor section: ...

## Problem
...
## Current behaviour
...
## Required change
...
## Acceptance criteria
...
## Docs
...
## Files
...
## Out of scope
...
```

- `status` uses the triage role strings from `docs/agents/triage-labels.md`
  (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`),
  plus two lifecycle states used while work is in flight: `in-progress` and `done`.
- `blocked_by` lists ticket ids (e.g. `["0001", "0002"]`). A ticket is unblocked when
  every listed ticket is `done` or `wontfix`. An empty list means ready.
- Sections are a guide, not a schema — adapt them (a research ticket may be just the
  question plus an `## Answer`).

## Operations

- **Publish / create a ticket**: write `docs/tickets/NNNN-<slug>.md` using the next
  free number, then add a row to `docs/tickets/README.md`. No CLI involved.
- **Read a ticket**: read the referenced file. The user will normally pass the path or
  the id directly.
- **List / query tickets**: read `docs/tickets/README.md`, or glob `docs/tickets/*.md`
  and filter on the `status`/`blocked_by` frontmatter fields.
- **Comment / append history**: add a `## Comments` heading at the bottom of the file
  (if absent) and append dated entries under it.
- **Change status**: edit the `status` field in the frontmatter and update the index.
- **Claim**: set `status: in-progress`.
- **Close**: set `status: done` (or `wontfix` if not actioned) and update the index.

## When a skill says "publish to the issue tracker"

Write a new ticket file under `docs/tickets/` and add it to the index.

## When a skill says "fetch the relevant ticket"

Read the file the user references (by path or id).

## Wayfinding operations

Used by `/wayfinder` (when installed). The **map** is one file with one **child** file
per ticket.

- **Map**: `docs/tickets/<effort>/map.md` — the Notes / Decisions-so-far / Fog body.
- **Child ticket**: `docs/tickets/<effort>/NN-<slug>.md`, numbered from `01`. A `Type:`
  frontmatter field records the ticket type (`research`/`prototype`/`grilling`/`task`);
  `status` records `in-progress`/`done`.
- **Blocking**: the `blocked_by` frontmatter list, or a `Blocked by:` line in the body.
  A ticket is unblocked when every file it lists is `done`.
- **Frontier**: scan `docs/tickets/<effort>/` for files that are open, unblocked, and
  unclaimed; first by number wins.
- **Claim**: set `status: in-progress` before any work.
- **Resolve**: append the answer under an `## Answer` heading, set `status: done`, then
  append a context pointer (gist + link) to the map's Decisions-so-far in `map.md`.

## Note on the tooling template

The engineering-skills setup template (`issue-tracker-local.md`) defaults to
`.scratch/<feature>/`. This repo uses `docs/tickets/` instead so tickets are tracked
and committed with the code. The operations above are the authoritative convention.
