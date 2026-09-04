---
name: improve-codebase-architecture
description: Inspect a codebase for architectural friction and produce a static HTML report of module-deepening opportunities. Use only when explicitly invoked with /skill:improve-codebase-architecture.
disable-model-invocation: true
---

# Improve Codebase Architecture

Survey a codebase for **deepening opportunities**: refactors that turn shallow modules into deep ones. The result is a report for the user to choose from; do not implement a refactor as part of the survey.

## Pi compatibility

This skill is self-contained. Pi loads this file when the user runs `/skill:improve-codebase-architecture`; it does not provide a separate `Skill` tool and this workflow must not assume one exists.

Do the exploration yourself with the tools available in the current Pi session. Do not require a sub-agent. If related skills are installed, they may provide additional guidance, but their absence must not block the survey.

Use this architecture vocabulary exactly in suggestions:

- **module**, **interface**, **implementation**, **depth**, **deep**, **shallow**, **seam**, **adapter**, **leverage**, **locality**
- the **deletion test**: deleting a shallow module should concentrate complexity behind a smaller interface, rather than merely move it to callers
- **the interface is the test surface**
- **one adapter is a hypothetical seam; two adapters make it real**

Do not substitute `component`, `service`, `API`, or `boundary` when one of the vocabulary terms is meant.

## 1. Set the scope

Start with the user-provided direction when there is one. Otherwise, inspect a useful stretch of recent history with `git log --oneline` and use recurring paths as the initial scope. Widen the scan only when the history has no clear hot spot.

Read `AGENTS.md` first when it exists. Read `CONTEXT.md` and ADRs under `docs/adr/` before evaluating the relevant area. Use the domain language in those files: if the glossary defines `Order`, say `Order intake module`, not an invented implementation name.

## 2. Explore

Explore the selected area directly. Prefer focused file and content searches, then read the relevant source. Look for friction rather than mechanically counting files:

- Understanding a concept requires moving through many small modules.
- A module is shallow: its interface is nearly as complex as its implementation.
- Pure functions exist only for testability while bugs lie in their calling flow, reducing locality.
- Tightly coupled modules leak across a seam.
- Important behavior is hard to test through its current interface.

Apply the deletion test to every candidate. Keep only candidates where deletion would concentrate complexity. Do not surface generic cleanup, speculative rewrites without evidence, or design proposals that merely contradict an ADR without real present friction.

## 3. Write the report

Create `AI-output/` when it does not exist. Write one self-contained, static HTML report to:

```text
<repository-root>/AI-output/architecture-review-<timestamp>.html
```

Do not overwrite a previous report. Use the repository root determined from the current working tree. Tell the user the absolute report path; do not attempt to open a browser.

Follow [HTML-REPORT.md](HTML-REPORT.md). The report must use inline CSS and inline SVG only: no CDN, JavaScript, framework, or network dependency. Each candidate needs a before/after visualisation.

Every candidate card contains:

- **Files**: affected files or modules
- **Problem**: observed architectural friction
- **Solution**: a plain-language deepening proposal
- **Benefits**: stated in locality, leverage, and test-surface terms
- **Before / After**: side-by-side visualisation
- **Recommendation strength**: `Strong`, `Worth exploring`, or `Speculative`

If a candidate conflicts with an ADR and the current friction justifies revisiting it, mark that conflict explicitly. End with one top recommendation and why it is the best first option.

After writing the report, stop and ask: `Which candidate would you like to explore?` Do not propose an interface or change code yet.

## 4. Discuss a selected candidate

After the user selects a candidate, guide a concise decision discussion: constraints, dependencies, the deepened module's responsibility, what belongs behind the seam, and which tests survive through its interface.

The survey itself must not edit code, `CONTEXT.md`, or ADRs. If a durable naming decision or rejected candidate warrants documentation, explain the proposed change and ask the user for permission before editing it. Offer an ADR only when the rejection reason would prevent a future survey from repeating the same proposal.

If the user requests alternative interfaces, present two materially different designs, compare their locality and leverage, and let the user choose before making changes.
