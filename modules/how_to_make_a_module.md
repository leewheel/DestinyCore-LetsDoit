# Creating a module

Modules are self-contained pieces of content that live under `modules/` and are
discovered automatically by CMake. Any directory holding a `src/` subdirectory
is treated as a module.

## Quick start

Run the helper from this directory:

```sh
./create_module.sh
```

It copies `skeleton-module/` to a new module of your choosing and renames the
loader so it compiles right away.

## Doing it by hand

1. Copy `skeleton-module/` to `modules/<your-module>/`.
2. Rename the loader function in `src/MP_loader.cpp` to
   `Add<your_module>Scripts()` — the module directory name with every `-`
   replaced by `_`. This is the entry point the generated loader calls.
3. Put your config keys in `conf/<name>.conf.dist`. They are installed to the
   `modules` directory next to `worldserver.conf` and merged into the running
   config at startup (rename `.conf.dist` to `.conf` to activate).

## Build types

The `MODULES` CMake option sets the default linkage for every module:

- `static`  — compiled into the worldserver (default).
- `dynamic` — built as a separate shared library, hot-swappable at runtime.
- `none`    — modules disabled.

Override a single module with `-DMODULE_<NAME>=static|dynamic|disabled`, e.g.
`-DMODULE_SKELETON-MODULE=dynamic`.

## Advanced

A `modules/<name>/<name>.cmake` file, if present, is included inline during
configuration — use it to pull in extra dependencies or sources.
