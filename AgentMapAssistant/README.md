# Agent Map Assistant — natural-language map requests you can audit

Ask for map operations in plain language ("Show the buildings within 150 m of a school
in red") and watch them run as a validated, previewed and recorded GeoKernel workflow.
The model never runs code or touches the map directly: it only proposes a
**workflow DSL v2** plan (ADR-0007), and GeoKernel does the rest.

```
request ─► planner (LLM or offline demo) ─► proposal ─► validation ─► plan preview
          [Run / Confirm and run] ─► deterministic execution ─► results ─► provenance
          [Undo last] reverts the whole workflow
```

The chat panel is the SDK's `GisAgentPanel`; this example docks it beside a
`GisViewer`, loads a small neighbourhood and adds the planner, sample requests and a
provenance viewer. All application C++ is in `main.cpp`.

## Build and run

The agent workflow API ships after SDK 1.5.40. Until the next public SDK release,
configure with an SDK built from the GeoKernel sources:

```bat
cd /d D:\projects\GeoKernel.Examples.Qt\AgentMapAssistant
cmake --preset windows-msvc -DCMAKE_MAKE_PROGRAM=C:/Ninja/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 -DGEOKERNEL_ROOT=<sdk>
cmake --build --preset windows-msvc-release
outputs\windows\Release\bin\AgentMapAssistant.exe
```

Configure stops with a clear message when the SDK has no agent API
(`include/GeoKernel.Viewer/Agent/GisAgentPanel.h`). Once the next SDK is published the
`-DGEOKERNEL_ROOT` argument is no longer needed.

### Planner: offline demo or an LLM

Without configuration the example runs in **offline demo mode**: the six requests in
the **Try** list have prepared plans in [data/agent-demo.json](data/agent-demo.json),
exactly as a model would return them. They go through the same validation, preview,
confirmation, execution, undo and provenance as a model's proposal. Other requests are
answered with how to enable a model. No network access is needed.

To plan your own requests, set the provider before starting the example:

| Provider | Environment |
|---|---|
| Anthropic | `GEOKERNEL_LLM_PROVIDER=anthropic`, `ANTHROPIC_API_KEY` (model defaults to `claude-opus-5-5`) |
| OpenAI | `GEOKERNEL_LLM_PROVIDER=openai`, `GEOKERNEL_LLM_MODEL`, `OPENAI_API_KEY` |
| Ollama (local) | `GEOKERNEL_LLM_PROVIDER=ollama`, `GEOKERNEL_LLM_MODEL`, optional `GEOKERNEL_LLM_BASE_URL` |

The status bar shows which planner is active. API keys are read from the environment
when a request is made and never appear in the log, errors or provenance. The model
receives the request, the layer list with field names (grounding) and the field notes
from `agent-demo.json` — no feature data.

## Workflow

1. The map shows a synthetic neighbourhood: 352 buildings (`ref`, `use`, `floors`,
   `year_built`, `status`), 4 schools, 3 parks and 16 streets, in EPSG:4326.
2. Pick a request under **Try** and click **Ask**, or type one in the panel and click
   **Send**. Planning runs on a worker thread.
3. The **Plan** box shows every step, its operation, category, effects (reads data,
   modifies view, modifies data, destructive) and parameters before anything runs.
   A plan that fails validation is rejected with the reason and nothing runs.
4. **Run** executes a plan that only reads data or changes the view. A plan with steps
   that change or delete data shows **Confirm and run**: clicking it confirms that
   exact plan (its `planId`); a different plan needs its own confirmation.
5. Results appear in the log: tables row by row, measurements with units, feature
   counts per result layer. Result layers are named after their step ids and styled by
   the plan.
6. **Undo last** reverts every step of the most recent workflow (result layers, styles,
   labels, selections, edits, view).
7. **Provenance...** shows the hash-chained record of each executed workflow: the
   normalized plan, its validation decision, the input map hash and the SHA-256 of
   every step output, with the chain verified.

## Sample requests

| Request | Plan | Confirmation |
|---|---|---|
| Show the buildings within 150 m of a school in red | `buffer` → `select_by_location` → `set_style` ×2 | no |
| Label the buildings taller than 8 floors with their floor count | `filter` → `set_style` → `set_labels` | no |
| How many buildings of each use are there, and how many floors in total? | `aggregate` (table in the log) | no |
| What is the total park area in hectares? | `measure` → `zoom_to` | no |
| Flag the buildings built before 1975 near the primary avenue for inspection | `filter` → `buffer` → `select_by_location` → `filter` → `update_attributes` → style, labels | yes: modifies data |
| Make an editable copy of the buildings and delete the demolished ones from it | `filter` → `delete_features` → `set_visibility` | yes: destructive |

Edits always apply to result layers in memory. The GeoJSON files are never modified;
editing file layers from workflows is refused by the executor.

## Data

`data/*.geojson` is a synthetic, deterministic neighbourhood generated for this example
(street names are borrowed from Kadıköy, Istanbul; geometry and attributes are invented).
It is copied next to the executable at build time and can be regenerated with
`python tools/make_demo_data.py data`. `data/agent-demo.json` holds the grounding notes and
the prepared plans.

## Related

- ADR-0007 — agent workflow DSL v2, effects, confirmation, provenance
- `geokernel-mcp` — the same operations as MCP tools for Claude Desktop / Claude Code
- The .NET, Python, Electron and Rust versions of this example use the agent C ABI
  (`GisAgentInterop.h`) through their language bindings.
