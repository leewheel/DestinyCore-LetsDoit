# Skeleton module

A minimal example module for the core's module system. It greets every player
with a "Hello world" chat message on login, gated by a config option.

## Layout

```
skeleton-module/
├── conf/
│   └── skeleton.conf.dist     # installed to <config>/modules/, rename to .conf to use
└── src/
    ├── MP_loader.cpp          # module entry point (Add<module>Scripts)
    └── SkeletonPlayer.cpp     # the actual PlayerScript
```

## How it builds

The build picks up every directory under `modules/` that contains a `src/`
folder. A module is linked according to the `MODULES` CMake option (`static`,
`dynamic` or `none`), or overridden per module with `-DMODULE_SKELETON-MODULE=...`.

The loader function name is derived from the directory name with every `-`
replaced by `_`, so `skeleton-module` must expose `Addskeleton_moduleScripts()`.

## Config

Files matching `conf/*.conf.dist` are copied next to the worldserver config in a
`modules/` subdirectory. Rename `skeleton.conf.dist` to `skeleton.conf`; the
core auto-loads every `*.conf` in that directory and merges the keys into the
main config, so `sConfigMgr->GetBoolDefault("SkeletonModule.Enable", true)`
just works.
