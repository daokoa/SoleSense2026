# SoleSense Pseudocode

Two pseudocode documents describing the system, at different stages. Both are kept because they cover different scopes.

| File | Lines | Scope | Purpose |
|---|---|---|---|
| [`system-flow.md`](system-flow.md) | 137 | High-level system flow | Earlier sketch — covers boot, recording loop, frontend pages, and analysis pipeline as a single readable narrative. Good orientation document for new contributors. |
| [`injury-analysis.md`](injury-analysis.md) | 437 | Detailed analysis algorithms | Later expansion — full pseudocode for the 7 injury-flag detection algorithms, with thresholds, justifications, and citations. Reads like the spec for the JS analysis pipeline that runs in the browser after a recording. |

Neither file is executable code. Treat them as design documents that the implementation should reflect.

The canonical project spec is [`SOLESENSE.md`](../../SOLESENSE.md) — when there's a conflict between these pseudocode files and `SOLESENSE.md`, `SOLESENSE.md` wins.
