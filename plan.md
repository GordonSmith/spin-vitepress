## Plan: Compile-once DuckDB "stored procedure" as a C ABI library in wasm/webassembly

**TL;DR:** Most of what you want is possible with the existing APIs. A prepared statement already stores the finished physical plan ([client_context.cpp](src/main/client_context.cpp#L313-L314)). After that, each call only binds parameters and executes. Physical plans can't be saved to a file, and adding that would take a lot of work. Spin needs a different trick anyway, because it starts a fresh Wasm instance for every request, so nothing stays warm between calls. The plan is to use Wizer pre-initialization:

## Goal

Add one fixed, read-only DuckDB lookup to this repository, expose it through a small C ABI, and package it as a single-threaded Spin HTTP component. Prepare the SQL once per Wasm instance and use Wizer only after the existing Emscripten build proves that parameterized execution is fast enough.

This is not a new DuckDB port. The repository already provides most of the engine-side foundation:

- `packages/duckdb` builds DuckDB 1.5.6 from `vcpkg-overlays/duckdb`.
- The current vcpkg manifest statically includes only `autocomplete` and `json`; `httpfs`, networking, and remote storage are not in the build.
- `packages/duckdb/CMakeLists.txt` already enables Wasm exceptions, WasmFS, and the Emscripten filesystem.
- `packages/duckdb/src-cpp/main.cpp` already exposes connections, prepared statements, typed parameter conversion, execution, and materialized results through Embind.
- `packages/duckdb/tests/duckdb.spec.ts` already tests prepared parameters and files registered in WasmFS.
- `packages/duckdb/src-cpp/stubs.c` already carries the single-threaded Emscripten compatibility shim.

The missing work is procedure-specific code, measurement, a WASI toolchain/target, and Spin component packaging.

## Repository layout to add

Keep the generic `@hpcc-js/wasm-duckdb` API intact. Add the procedure as a separate target that shares a small engine-independent core:

```text
packages/duckdb/
   src-cpp/
      proc/
         proc.h                 # C ABI and status codes
         proc.cpp               # DuckDB open/prepare/bind/execute logic
         proc_config.h          # fixed SQL, parameter and output contract
   tests/
      proc.spec.ts             # Emscripten correctness/plan tests
      proc.bench.ts            # opt-in latency benchmark
   spin/
      handler.cpp              # wasi:http adapter; no DuckDB logic
      spin.toml
      wit/                     # generated or pinned HTTP bindings
   fixtures/proc/
      build.sql                # deterministic database construction
      expected.json
scripts/
   build-proc-db.sh
   build-duckdb-wasi.sh
```

Do not put the procedure contract into the existing `DuckDB` TypeScript class. The browser/Node package remains a general SQL API; the new C ABI and Spin artifact are deployment-specific.

## Phase 0: Freeze the procedure contract

Before implementation, replace the placeholders in `proc_config.h` with the actual:

- SQL text and explicit casts for every parameter.
- Parameter C types, nullability, and accepted ranges.
- Output columns and wire representation.
- Maximum result rows/bytes and behavior when the output buffer is too small.
- Database schema version expected by the component.

Use integer/status returns across the C ABI. Store the last error on the handle and expose `proc_error`; do not let C++ exceptions cross either the C ABI or component boundary.

Proposed ABI:

```c
typedef struct proc_handle proc_handle;

int32_t proc_open(const uint8_t *db_data, size_t db_size, proc_handle **out);
int32_t proc_call(proc_handle *handle, /* typed parameters */,
                           uint8_t *output, size_t output_size, size_t *output_written);
const char *proc_error(const proc_handle *handle);
void proc_close(proc_handle *handle);
```

Prefer a caller-owned output buffer over callbacks. It is easy to test natively, maps cleanly to Wasm linear memory, and avoids callback trampolines in the component adapter.

## Phase 1: Build a deterministic database fixture

Add `packages/duckdb/fixtures/proc/build.sql` and `scripts/build-proc-db.sh`.

1. Build the table with `CREATE TABLE ... AS SELECT ... ORDER BY key` so range lookups can use zonemap pruning.
2. Add a `PRIMARY KEY` or explicit ART index for point lookups.
3. Run `CHECKPOINT` and record the schema version in a metadata table.
4. Reopen the result read-only and run `PRAGMA database_size` plus an integrity/smoke query.
5. Build the same bytes twice and compare checksums. Investigate nondeterminism before embedding the file or relying on reproducible snapshots.

Use a host DuckDB 1.5.6 CLI for this build step, matching `vcpkg-overlays/duckdb/vcpkg.json`. Do not use the browser package to generate the artifact: its public API currently creates only an in-memory database and does not expose generated WasmFS files.

## Phase 2: Measure on the existing Emscripten target

This replaces the generic native baseline. The deployed code will be Wasm, and this repository already has a working DuckDB Wasm build.

1. Implement `proc.cpp` using the DuckDB C++ API already linked by `duckdblib`:
    - Create `DBConfig`, set `options.access_mode = AccessMode::READ_ONLY`, and force one worker thread.
    - Create one `DuckDB`, one `Connection`, and one `PreparedStatement` per `proc_handle`.
    - Validate the prepared statement once in `proc_open` and retain it for all calls.
    - Bind values with fixed DuckDB logical types so calls cannot trigger type-driven rebinds.
    - Materialize and encode only the contracted output columns.
2. Add a thin Embind test adapter for the C ABI to the existing `duckdblib` target. This adapter is test-only API; the procedure core itself must not include `emscripten::val`.
3. Register the fixture bytes with the existing WasmFS support and test:
    - Known keys and ranges against `expected.json`.
    - Missing keys, nulls, boundary values, and undersized output buffers.
    - Repeated calls on one handle.
    - Two independent handles to verify that connection/prepared-statement state is not shared.
4. In a Node Vitest, measure `proc_open` separately from warm `proc_call` over enough iterations to amortize timer noise. Keep this benchmark opt-in rather than a pass/fail CI timing test.
5. Execute `EXPLAIN ANALYZE` with the same typed parameters. Confirm an ART index scan for point lookup, or row-group pruning for a range lookup. Save the relevant plan text with the benchmark output.

Gate: proceed to WASI only if warm calls use the intended access path and preparation is a meaningful part of startup. If bound parameters defeat pushdown, first add explicit casts or adjust the SQL. Do not begin Wizer work around a bad query plan.

## Phase 3: Add a separate WASI build

The existing `build/` tree, `wasm32-emscripten` triplet, and `duckdblib` target are Emscripten-specific and cannot produce a Spin component. Leave them unchanged.

1. Pin wasi-sdk, Wizer, `wit-bindgen`, `wasm-tools`, and the Spin CLI versions in a bootstrap script or documented tool manifest. `package.json` already removes `wasi-sdk` in `uninstall-build-deps`, but there is no installer or WASI preset yet.
2. Add a `wasm32-wasi` overlay triplet and a separate CMake binary directory such as `build-wasi`; never reuse `build/build.ninja`.
3. Reuse the pinned DuckDB 1.5.6 overlay, but give the WASI build a minimal manifest feature set. The procedure does not need the current `autocomplete` extension; retain `json` only if the fixed SQL or output encoder needs it.
4. Configure DuckDB single-threaded and disable extension autoload/install for this target. The current overlay sets both to `1`, so make those settings target/triplet options rather than changing behavior for the published Emscripten package.
5. Compile `proc.cpp` plus `spin/handler.cpp` without Embind, `stubs.c`, Emscripten flags, or Emscripten filesystem APIs.
6. Generate the `wasi:http` bindings, link a core Wasm module with the Wizer init export, run Wizer, then componentize it with the matching WASI adapter. Record this exact pipeline in `scripts/build-duckdb-wasi.sh`.

The first WASI milestone is deliberately smaller than Spin: a core module that opens the fixture, calls the procedure, and returns expected bytes under Wasmtime. This isolates libc, exception, and filesystem failures from component binding failures.

## Phase 4: Make the Wizer snapshot self-contained

Do not snapshot an open WASI file descriptor. Wizer restores linear memory and globals, not a live host descriptor.

Use this order of preference:

1. **Small/medium database:** embed the fixture bytes in the module, import them into a DuckDB `:memory:` database during the Wizer init function, build the ART index, detach/close the source, and then prepare the statement. The snapshot contains DuckDB-owned pages and no host file handle.
2. **Large database:** mount the read-only file through Spin and open/prepare it when each pooled instance is initialized. Measure this before writing a custom DuckDB filesystem. Wizer can still snapshot extension/catalog initialization that does not retain file descriptors.
3. **Only if startup remains unacceptable:** implement a read-only DuckDB `FileSystem` over embedded bytes whose state is entirely in linear memory, then snapshot the open database and prepared statement.

Add a post-Wizer test that instantiates the snapshot in a fresh Wasmtime process with no fixture file available and executes at least two different parameter sets. That test is the proof that no build-time descriptor leaked into runtime state.

The Wizer init export must leave one initialized `proc_handle` in module state. The HTTP handler parses the request, calls `proc_call`, maps status codes to HTTP responses, and performs no SQL parsing or preparation.

## Phase 5: Spin integration and load test

1. Add `packages/duckdb/spin/spin.toml` with no outbound network permissions and a read-only files mount only if Phase 4 selects the mounted-file path.
2. Run `spin up` and verify success, invalid parameters, not-found results, oversized results, and repeated calls.
3. Load-test with fixed concurrency and report p50/p95/p99, throughput, errors, component size, snapshot size, and peak memory.
4. Compare:
    - Unsnapshotted instance initialization.
    - Wizer snapshot without an open file descriptor.
    - Warm calls within an already pooled instance.
5. Treat each instance as single-threaded. Scale with Spin/Wasmtime instance pooling; do not share a `Connection` or `PreparedStatement` across threads.

## Phase 6: Engine work only if measurements require it

Keep these out of the first implementation:

1. **Executor/pipeline reuse:** investigate the per-call `Executor::Initialize` cost only if it dominates the Phase 2 profile.
2. **Direct ART lookup:** bypass SQL with DuckDB internal storage APIs only if the prepared query remains too slow and the result contract is stable enough to accept coupling to DuckDB internals.
3. **Shared physical plans:** not useful for the Spin design, where each single-threaded instance owns its connection and prepared statement.
4. **Physical-plan serialization:** out of scope. It is unsupported upstream and unnecessary if the self-contained Wizer snapshot works.

Any DuckDB patch belongs in `vcpkg-overlays/duckdb`, requires a `port-version` bump, and must be validated after forcing the `duckdb` vcpkg port to rebuild. Do not edit generated sources under `build/vcpkg_buildtrees`.

## Validation commands

For the existing package baseline:

```bash
npm run build-cpp
npm run build-ws
npm run lint
cd packages/duckdb && npm test
```

For focused iteration, add scripts so these commands become available:

```bash
cd packages/duckdb
npm run test-proc
npm run bench-proc
npm run build-wasi
npm run test-wasi
npm run test-spin
```

The C++ build can take 30-60 minutes. If the DuckDB overlay or its build options change, remove the installed `duckdb` port using the repository's selective vcpkg cleanup procedure before rebuilding; a normal incremental build may otherwise reuse the old package.

## Remaining risks and early checks

Most generic DuckDB-on-Wasm concerns are already covered by this repository. The unresolved risks are specific to the new runtime:

1. **WASI exception compatibility:** `-fwasm-exceptions` works in the Emscripten artifact, but the pinned wasi-sdk, Wizer, component adapter, and Spin/Wasmtime chain must preserve Wasm EH. Prove a thrown-and-caught C++ exception in the Phase 3 smoke module before compiling all of DuckDB.
2. **WASI libc gaps:** `packages/duckdb/src-cpp/stubs.c` is Emscripten-specific. Compile DuckDB unchanged first, then add narrowly scoped WASI shims only for symbols actually missing from the pinned toolchain.
3. **Snapshot size:** importing the database into `:memory:` may make the component too large. Set an explicit deployment budget after measuring the real fixture; choose the mounted-file path if it exceeds that budget.
4. **Snapshot determinism:** verify fresh-process results and checksum repeated Wizer outputs where practical. Do not assume an open DuckDB object is snapshot-safe merely because initialization succeeds.
5. **Parameterized access path:** this remains a query-specific concern and is settled by the Phase 2 `EXPLAIN ANALYZE`, not by an engine change in advance.

## Decisions

- Reuse DuckDB 1.5.6 and the existing vcpkg overlay; do not introduce another DuckDB source tree.
- Validate behavior in the current Emscripten package before adding WASI infrastructure.
- Keep the published Embind API and its build unchanged.
- Put procedure logic in a C++ core with a no-throw C ABI; use thin Embind and Spin adapters.
- Use one database, connection, and prepared statement per Wasm instance.
- Prefer a self-contained in-memory Wizer snapshot for modest data, with a mounted read-only file as the size fallback.
- Do not serialize physical plans or modify DuckDB internals without profiling evidence.
1. Build with `make reldebug`. Write a small benchmark that prepares the lookup once, then calls it N times on 1 thread and on T threads with one connection each. Record the time per call for prepare and for execute separately.
2. Run `EXPLAIN ANALYZE` on the prepared statement with `$1`/`$2` parameters to check that the filter is pushed into the scan. A point lookup should use the ART index scan (`TryScanIndex` in [table_scan.cpp](src/function/table/table_scan.cpp#L905)). A range lookup should rely on zonemap pruning over data sorted by the key. If parameters stop either from happening, that becomes the first issue to solve.

**Phase 1: Build the read-only index/payload file** *(parallel with Phase 0)*
3. Write a build script that does the following:
   - Runs `CREATE TABLE … AS SELECT … ORDER BY key`, so zonemaps work for range lookups.
   - Adds a `PRIMARY KEY`/ART index for point lookups.
   - Runs `CHECKPOINT`.
   - Optionally defines `CREATE MACRO proc(a, b) AS TABLE …` as the documented definition of the procedure.
4. Open the file with `access_mode=READ_ONLY` ([config.hpp](src/include/duckdb/main/config.hpp#L87)).

**Phase 2: C ABI wrapper (native first)** *(depends on 1)*
5. A small library on top of the C API in [duckdb.h](src/include/duckdb.h) with three functions:
   - `proc_open(path)`: calls `duckdb_open_ext` with READ_ONLY and `threads=1`, then a connection, then `duckdb_prepare` of the burnt-in SQL string.
   - `proc_call(handle, typed params…, out callback/buffer)`: calls `duckdb_bind_*` and `duckdb_execute_prepared`, then copies rows out.
   - `proc_close`.
6. For native concurrency, use one connection plus its own prepared statement per worker thread. A `PreparedStatement` is tied to a single `ClientContext` and can't be shared between threads.

**Phase 3: WASI port, Wizer and the Spin component** *(depends on 2; largest risk)*
7. Add a WASI build using wasi-sdk:
   - `DUCKDB_NO_THREADS`, `-fwasm-exceptions`, and the smallest set of in-tree extensions.
   - Turn off network, extension loading and the http transport ([http_transport_manager.cpp](src/main/http/http_transport_manager.cpp#L9)).
   - Patch local-filesystem calls that WASI libc lacks (e.g. file locking).
8. Choose how the DB file is accessed: either a Spin `files` mount, or embedding the bytes into memory (more robust with Wizer).
9. Write a Wizer init export that runs `proc_open`, so the snapshot holds the open DB, the connection and the prepared plan. The Spin HTTP handler then only parses the request parameters and runs `proc_call`.
10. Scale concurrency through Spin/Wasmtime instance pooling: one single-threaded instance per request.

**Phase 4: Engine changes, only if Phase 0/3 numbers justify them** *(ordered by value per effort)*
11. **Direct lookup fast path:** a C++ entry point that skips SQL entirely, doing the ART lookup on the key and fetching the matching rows with the internal `DataTable` API. This is the true "burnt-in job" for point lookups.
12. **Cache the execution setup:** reuse the executor and pipeline setup between calls of the same prepared plan (`Executor::Initialize`, [executor.cpp](src/parallel/executor.cpp#L225-L261)).
13. **Share one physical plan across connections:** only matters natively. Spin is single-threaded per instance, so it doesn't help there.
14. Physical plan serialization is explicitly not planned: there is no infrastructure for it, and the Wizer snapshot makes it unnecessary.

**Relevant files**
- [src/main/prepared_statement_data.cpp](src/main/prepared_statement_data.cpp#L96): `RequireRebind`, the conditions that force a re-plan (parameter type changes, catalog changes).
- [src/main/client_context.cpp](src/main/client_context.cpp#L313-L314): where the physical plan is stored in the prepared statement.
- [src/main/client_context_execution.cpp](src/main/client_context_execution.cpp#L263): `executor.Initialize`, the setup cost paid on every call.
- [src/function/table/table_scan.cpp](src/function/table/table_scan.cpp#L1025): index-scan settings `index_scan_percentage`/`index_scan_max_count`.
- [src/planner/binder/query_node/bind_table_macro_node.cpp](src/planner/binder/query_node/bind_table_macro_node.cpp): table macros are bound again on every call, so call them through a prepared statement rather than directly.
- [Makefile](Makefile#L489-L504) and [CMakeLists.txt](CMakeLists.txt#L348): existing emscripten Wasm targets to use as a template for a WASI target.
- [src/parallel/task_scheduler.cpp](src/parallel/task_scheduler.cpp#L363): the no-threads/emscripten code path.

**Verification**
1. Phase 0: benchmark output shows prepare time vs execute time per call. `EXPLAIN ANALYZE` shows an index scan or row groups being pruned, with parameters bound.
2. Phase 2: a C test calls `proc_call` from T threads with no errors and results matching plain SQL.
3. Phase 3: run `spin up` and load-test it (e.g. with `hey`/`oha`). Compare cold vs warm latency with and without Wizer, and check module size and snapshot memory size.
4. Phase 4, if done: the fast path returns the same results as SQL for a randomized set of keys, and latency improves noticeably.

**Decisions**
- Prototype on the existing APIs first. Change the engine only if per-call setup overhead dominates.
- Expose a C ABI. Target Spin (WASI component), with one single-threaded instance per request.
- Out of scope: write paths, remote/S3 storage, a general "compile any SQL to code" tool.

**Risks to check early**
1. **Exceptions:** DuckDB relies heavily on C++ exceptions. The installed wasi-sdk and Spin's Wasmtime must both support the Wasm exception-handling proposal. If they don't, Phase 3 is blocked. Check this before investing in Phase 3.
2. **Snapshot validity:** Wizer can't snapshot open file handles, and DuckDB may record time or random values during startup. Embedding the DB in memory avoids the file-handle issue; startup-time values need checking.
3. **Parameters vs pushdown:** a filter on `$1` might not get pushed into the scan. If so, the workaround is the Phase 4 fast path, or re-preparing with constants for each call.
