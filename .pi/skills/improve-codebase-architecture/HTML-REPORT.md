# HTML Report Format

Render the review as one static, self-contained HTML file in `AI-output/architecture-review-<timestamp>.html`. It must work offline: use only semantic HTML, inline CSS, and inline SVG. Do not use a framework, JavaScript, external stylesheet, CDN, or remote font.

## Scaffold

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Architecture review for {{repo name}}</title>
    <style>
      :root {
        color: #172033;
        background: #f8fafc;
        font-family: system-ui, sans-serif;
      }
      body { margin: 0; }
      main { max-width: 1100px; margin: 0 auto; padding: 48px 24px; }
      .card { margin-top: 28px; padding: 24px; border: 1px solid #dbe3ef; border-radius: 12px; background: #fff; }
      .diagrams { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 20px; }
      .diagram { min-height: 280px; padding: 16px; border: 1px solid #dbe3ef; border-radius: 8px; background: #f8fafc; }
      .badge { display: inline-block; padding: 3px 8px; border-radius: 999px; font-size: 0.8rem; font-weight: 700; }
      .strong { color: #065f46; background: #d1fae5; }
      .worth-exploring { color: #92400e; background: #fef3c7; }
      .speculative { color: #334155; background: #e2e8f0; }
      .files { color: #475569; font-family: ui-monospace, monospace; font-size: 0.9rem; }
      .adr-warning { padding: 12px; border-left: 4px solid #d97706; background: #fffbeb; }
      @media (max-width: 700px) { .diagrams { grid-template-columns: 1fr; } }
    </style>
  </head>
  <body>
    <main>
      <header>...</header>
      <section id="candidates">...</section>
      <section id="top-recommendation">...</section>
    </main>
  </body>
</html>
```

## Header

Show the repository name, date, and a compact legend: solid box = module, dashed line = seam, red arrow = leakage, thick dark box = deep module. Skip a generic introduction; move straight to the candidates.

## Candidate card

Each candidate is one `<article class="card">`.

- **Title**: name the deepening, for example, `Collapse the Order intake pipeline`.
- **Recommendation strength**: `Strong`, `Worth exploring`, or `Speculative`, using the matching badge class.
- **Files**: a compact monospaced list.
- **Before / After**: two side-by-side diagrams. Use inline SVG for arrows, dependency graphs, or call flows; use HTML boxes for modules.
- **Problem**: one sentence describing observed friction.
- **Solution**: one sentence describing the proposed deepening.
- **Wins**: short bullets, six words or fewer.
- **ADR callout**: only when a real conflict merits reopening an ADR.

Keep prose sparse. If a diagram needs a paragraph to be understood, redraw it.

## Diagram patterns

Choose the simplest visual that makes the candidate concrete. Vary patterns across the report where it helps understanding.

### Boxes and arrows

Use a relative container with module boxes and an inline `<svg>` overlay. Use solid rectangles for modules, a dashed SVG path for a seam, and a red path for leakage. This is the default pattern for dependencies and call flow.

### Cross-section

Stack horizontal bands to show a call passing through thin modules. In the after view, replace several thin bands with one deep module whose internal implementation is muted.

### Mass diagram

Draw two rectangles per module: one for the interface surface and one for the implementation. A shallow module has similarly sized rectangles; a deep module has a short interface and a much taller implementation.

### Call-graph collapse

Show nested calls in the before view. In the after view, collapse them behind one interface and draw the internal calls faded inside the deep module.

## Style

Use generous whitespace and one restrained accent colour. Reserve red for leakage and amber for ADR warnings. Make diagrams about 320px tall where practical so before and after remain comparable.

## Top recommendation

End with one larger card: candidate name, one sentence explaining its leverage, and an anchor link to the candidate. Do not include extra dashboard elements or interaction.

## Vocabulary and tone

Use exactly: **module**, **interface**, **implementation**, **depth**, **deep**, **shallow**, **seam**, **adapter**, **leverage**, **locality**.

Avoid `component`, `service`, `API`, and `boundary` when the vocabulary has a precise term.

Good phrasing:

- `Order intake module is shallow: interface nearly matches implementation.`
- `Pricing leaks across the seam.`
- `Deepen: one interface, one place to test.`
- `Two adapters justify the seam: HTTP in production, in-memory in tests.`

Wins bullets should identify a concrete gain, such as `locality: bugs concentrate in one module` or `leverage: one interface, many callers`. Do not substitute vague claims such as `cleaner code` or `easier to maintain`.
