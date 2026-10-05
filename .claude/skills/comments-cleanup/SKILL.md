---
name: comments-cleanup
description: Remove redundant, stale or misleading source comments and fix outdated ones without changing behavior. Use only when the user explicitly asks for a comment cleanup of specific files or directories.
disable-model-invocation: true
---

# Clean Up Unneeded Comments

Clean up comments in this codebase so that comments remain useful, accurate, and consistent with the current implementation.

## Goals

- Remove comments that are redundant, obvious, outdated, misleading, or unrelated to the current code.
- Keep comments that help AI systems and human developers understand:
  - Why the code exists.
  - Non-obvious business rules or constraints.
  - Important trade-offs and design decisions.
  - Invariants and assumptions.
  - Edge cases that are not clear from the code.
  - External API, protocol, security, performance, or compatibility requirements.
  - Workarounds for known bugs or limitations.
- Update comments that are still valuable but no longer accurately describe the code.
- Prefer clear, self-explanatory code over comments that merely describe what the code does.

## Rules

1. Do not change application behavior.
2. Do not change public APIs, exported names, function signatures, database schemas, configuration formats, or file structure unless required to correct a comment.
3. Do not remove documentation comments that are part of a public API or generated documentation.
4. Preserve required legal notices, licenses, attribution notices, generated-file markers, and tool directives.
5. Preserve comments required by linters, compilers, formatters, bundlers, migrations, or other build tools.
6. Treat comments as potentially stale. Verify every retained comment against the implementation.
7. Do not add comments unless they explain something genuinely non-obvious.
8. Do not rewrite code only to make comment removal easier.
9. Keep comments concise and specific.
10. Avoid comments that restate the immediately following code.

## Comments to Remove

Remove comments such as:

- `// Increment i`
- `// Return the result`
- `// Check if the value exists`
- Comments that repeat a function, variable, class, or type name.
- Comments describing straightforward control flow.
- Comments left over from deleted or refactored code.
- Comments that describe an old implementation.
- Comments that contradict the current implementation.
- Commented-out code, unless it documents a deliberate compatibility or migration reason.
- TODOs that are already completed.
- Personal notes, temporary debugging comments, and conversational remarks.
- Excessive section headers that add no useful information.
- Comments that could be replaced by a clearer name or small refactor, without changing behavior.

## Comments to Keep or Improve

Keep comments when removing them would make the code materially harder to understand.

Examples include:

- Why a surprising implementation is necessary.
- Why a seemingly simpler approach is intentionally avoided.
- A business rule that cannot be inferred from the code.
- A security or privacy constraint.
- A performance consideration.
- An ordering, timing, concurrency, or lifecycle requirement.
- A workaround for an external dependency or platform-specific behavior.
- An invariant that must remain true.
- A compatibility requirement for older clients, data, or protocols.
- A non-obvious edge case.
- A migration note that is still active and actionable.

When useful, rewrite comments using this format:

`// Why this is necessary:`
`// [brief explanation of the constraint, decision, or behavior]`

Do not use comments to explain what the code already clearly expresses.

## TODO and FIXME Comments

For each TODO, FIXME, HACK, or similar marker:

  - Remove it if the issue is no longer relevant.
  - Keep it if it describes a real, unresolved issue.
  - Rewrite it so that it clearly explains the problem and desired outcome.
  - Do not invent issue numbers, deadlines, owners, or requirements.
  - Do not turn a comment into a task unless the existing code provides enough context.

## Workflow

  - Inspect the repository structure and identify the relevant source files.
  - Check project-specific instructions before editing.
  - Review comments in the target files.
  - Compare each comment with the current implementation.
  - Remove comments that are unnecessary or inaccurate.
  - Rewrite valuable but stale or unclear comments.
  - Preserve required directives, licenses, and generated-file markers.
  - Run the narrowest relevant formatter, linter, type checker, or test suite.
  -  Review the diff and confirm that only comment cleanup and directly necessary comment corrections were made.

## Quality Checklist

Before finishing, verify that:

  - No retained comment contradicts the code.
  - No useful explanation of a non-obvious decision was removed.
  - No comment merely repeats obvious code.
  - No required tool directive or legal notice was removed.
  - No commented-out code remains without a clear reason.
  - No application behavior changed.
  - The resulting comments are concise, specific, and written for both human developers and AI coding assistants.
  - The diff contains no unrelated formatting or refactoring changes.

## Final Response

Summarize:

  - Which files were changed.
  - The types of comments removed or rewritten.
  - Any unresolved TODOs or potentially stale comments that were intentionally preserved.
  - Which validation commands were run and whether they passed.

