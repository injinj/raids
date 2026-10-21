# AGENTS.md — raids

Rai Distribution Services: a redis-protocol server on raikv's shared
memory hash table (`ds_server`), with pub/sub, streams, HLL, geo (h3),
RDB load (`rdbparser`), an HTTP/websocket front, a console (`linecook`)
and a client API. It is the reference for how to use raikv's
`EvKeyCtx`/`EvPrefetchQueue` prefetch pipeline. Namespace `rai::ds`.
Depends on raikv, raimd, rdbparser, h3lib, libdecnumber, linecook (+ lzf,
pcre2 vendored).

## Workspace layout (injinj fleet convention)

- This repo is one of the `injinj` siblings. Every dependency is a sibling
  checkout (`../raikv`, `../raimd`, `../rdbparser`, `../h3`, `../libdecnumber`, `../linecook`); headers come from
  `-I../<dep>/include` and libraries from
  `../<dep>/FC43_x86_64/lib64/lib<dep>.a`. There is no install step between
  siblings — build the dependency, then build this repo.
- Dependencies are declared once, in `build_depends.mak`
  (`<dep>_dep := MAJOR.MINOR`, the rpm BuildRequires minimum). `./deps.sh`
  clones missing siblings from it, `./deps.sh -b` also builds them in
  dependency order, `./deps.sh -s` prints the commit of this repo and every
  sibling (the only record of what a release was built from).
- Whole-fleet build order lives in `~/injinj/GNUmakefile` (`make build`):
  openpgm → raikv → libdecnumber → raimd → linecook → lzf → rdbparser → h3 →
  HdrHistogram_c → sassrv → raids → natsmd → omm → raims → rvcache →
  raimdapi → ossl.

## Build

- `make` → `FC43_x86_64/bin`, `FC43_x86_64/lib64`. The build dir is
  `$(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)`: Fedora 43 →
  `FC43_x86_64`, Rocky 9.5 → `ROCKY9.5_x86_64`.
- `make port_extra=-g` → debug build in `FC43_x86_64-g`.
  `make port_extra=-mingw` → Windows cross build in `FC43_x86_64-mingw`
  (mingw64 toolchain; links the siblings' *master* checkouts).
- `NO_STL=1` builds with `-fno-rtti -fno-exceptions` and links with the C
  compiler; library code is written to survive that (error returns, no
  exceptions, no RTTI). Keep it that way.
- `make -j8` is safe; the makefile dependencies are complete. One trap: the
  **first `-j` run right after a compile error** can fail test executables
  with bogus undefined references (symbols from this repo's own lib)
  because they link against a library left half-updated by the failed run.
  Run plain `make` once, then `-j` works again. It is not a missing
  dependency.
- Warnings are clean. A new warning is a defect.
- If a sibling's public headers changed, rebuild the sibling first. A link
  warning like `lib<dep>.so.1.N, needed by ../<x>/.../lib<x>.so, not found`
  means sibling `<x>` was built against an older `<dep>` — rebuild `<x>`;
  at run time that warning is a hard failure.
- Ad hoc test program: capture the makefile's own lines once —
  `touch src/<any>.cpp; make -n | grep -- '-c src/<any>.cpp'` and
  `make -n | grep -- '-o FC43_x86_64/bin/' | head -1` — then reuse the
  compile line (minus `-c/-o`) and the link line's `-Wl,-rpath` list +
  `FC43_x86_64/lib64/libraids.a` + the lib tail. Run with
  `LD_LIBRARY_PATH` covering each sibling's `lib64`.

## Versioning and release

- Version is `major_num.minor_num.patch_num-build_num` in `.copr/Makefile`
  (included by the GNUmakefile). `make spec_version` prints the
  `raids_dep := MAJOR.MINOR` line dependents pin in `build_depends.mak`.
- `rpm/raids.spec` carries placeholders (`Version: 999.999`, release
  `99999`, `BuildRequires: <dep> _<dep>_dep`); `make srpm` substitutes them
  from `build_depends.mak`. Keep the `_ensure_git` line in
  `.copr/Makefile`: COPR's `make_srpm` chroot has no git and every
  `$(shell git …)` would silently expand empty. A missing `<dep>_dep`
  entry fails the COPR build at install time, not link time.
- COPR: `injinj/dev` builds the `dev` branch of github.com/injinj/raids
  on push (webhook); `injinj/rel` builds github.com/raitechnology/raids.
  Remotes: `origin` = raitechnology (release), `ii` = injinj (dev).
  Chroots: epel-7/8/9/10, fedora-43/44/45/rawhide, x86_64.
- ABI convention: a dependent's `Requires` is `MAJOR.MINOR`, its
  `BuildRequires` the full version, and MINOR is bumped with any change to
  the C++ ABI of a public header, so a rebuilt dependent never silently
  loads an old `.so`. Bump `minor_num` here when public headers change,
  then re-pin and rebuild dependents.
- Link only against installable sibling libraries (`$(lnk_lib)`), never a
  sibling's object files; if a binary needs a sibling helper, move it into
  the sibling's library and release the sibling first.
- Signed rpms are published to https://www.raitechnology.com/repo/
  (COPR rel → download → sign → createrepo → rsync; tooling in `~/injinj/rel`).

## Code conventions

- C++11, two-space indent, `/* … */` block comments, aligned member
  comments; snake_case functions/variables, CamelCase types. System headers
  first, then project headers.
- Errors are return codes / status enums; no exceptions, no RTTI in library
  code. Raw pointers and placement-new over the repo's allocators are the
  norm; copy how neighbouring code allocates.
- Keep the generated `CMakeLists.txt` out of your edits — it is derived
  from the GNUmakefile (and slated for removal).

## Layout

- `include/raids/` — `ev_redis`/`redis_*` command families (one header per
  redis type: list, set, zset, hash, stream, hll, geo, pubsub, keys),
  `ev_http`, `ev_client`, the console.
- `src/` (33 files) mirrors the command families.
- `doc/ds_server.1.md` (man page source), `doc/redis_cmd.adoc` (command
  coverage table). `config/raids.service`, `config/limit.conf` — systemd
  unit and limits for a packaged install.
- `test/` — `test_r*` per type, `test_tcp_ssl`, `test_auth`,
  `test_hll*`, `shmdp` (shm dump), `redis_cmd`.
- bins: `ds_server`, `ds_client`, `ds_pubsub_api`, `ds_test_api`.

## Contracts and gotchas

- Redis values live in the raikv hash entry; the entry type byte is the
  redis type. The same byte is used by other services for message codec
  ids, so a map is either a raids map or a cache map — don't mix.
- Prefetch: commands are decoded into `EvKeyCtx` objects and pushed
  through `EvPrefetchQueue` before execution so the hash lookups overlap;
  a new command must follow that two-phase shape or it serialises the
  pipeline.
- README "Testing performance" section documents the `redis-benchmark`
  and `wrk` loading setups and the numbers to expect.
  <!-- TODO: condense the expected numbers here -->

## Tests

- Per-type `test_r*` executables plus the redis test suite via
  `redis_cmd`. <!-- TODO: canonical invocation -->

## Open items / history

<!-- TODO: fill from git log -->
