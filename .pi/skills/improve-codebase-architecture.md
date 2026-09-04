## Improve Codebase Architecture for Pi

`improve-codebase-architecture` surveys a codebase for **deepening opportunities**: places where a shallow module (an interface nearly as complex as its implementation) could become a deep module. It produces a static HTML report and does not implement the refactor.

## Run it

In Pi, invoke the skill explicitly:

```text
/skill:improve-codebase-architecture
```

You can give it a direction:

```text
/skill:improve-codebase-architecture Focus on article routing and the standalone sidebar.
```

The skill has `disable-model-invocation: true`, so ordinary conversation does not trigger it automatically. This is deliberate: an architecture survey should be a user-requested activity.

## Output and scope

The report is created at:

```text
AI-output/architecture-review-<timestamp>.html
```

It is fully static and works offline: its styles and diagrams use inline CSS and SVG, with no JavaScript, framework, or CDN dependency. The survey reads the codebase and its relevant domain documentation, but does not change source files, `CONTEXT.md`, or ADRs.

After the report, Pi asks the user to choose one candidate. Only then does it discuss constraints, seams, interfaces, and tests. Documentation changes require the user's approval.

## What it evaluates

A candidate must pass the **deletion test**: removing the suspected shallow module must concentrate complexity behind a smaller interface rather than spread it among callers.

The report focuses on observed friction:

- Many small modules are needed to understand one concept.
- A module's interface nearly matches its implementation.
- Bugs sit in call flow rather than isolated pure functions, reducing locality.
- Details leak across a seam.
- Behavior is difficult to test through its interface.

Candidates are labeled `Strong`, `Worth exploring`, or `Speculative`, include before/after diagrams, and end with a single top recommendation.

## Pi-specific design

The skill is self-contained. Pi does not have a generic `Skill` tool and a Pi session may not provide sub-agents, so the workflow explores directly using the available file and search tools. It embeds the architecture vocabulary it needs: **module**, **interface**, **implementation**, **depth**, **deep**, **shallow**, **seam**, **adapter**, **leverage**, and **locality**.

Other optional skills can add context when installed, but they are not required for the report to complete.
