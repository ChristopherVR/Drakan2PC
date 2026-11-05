# Issue 1: optional build CPU affinity

Added `-CpuAffinity` to the canonical Windows builders and an optional hex-mask
field to the existing installer CPU page (`/AFFINITY=` also fills it). The guided
builder persists the choice with cores/priority; `default` clears it. Direct local
builds keep their existing defaults unless the option is supplied.

The shared PowerShell helper validates a nonzero 1–16 digit hex mask against the
calling process's permitted affinity before extraction/build work. It temporarily
sets the builder process affinity so compiler descendants inherit it, disables
MSBuild node reuse, and restores both settings in `finally`. Masks address logical
CPUs in the current Windows processor group, not physical cores or all groups.
Linux/WSL/browser requests fail explicitly; portable runtime code is untouched.

Validation: three Windows PowerShell subprocess tests pass, covering malformed,
zero, overflowing and unavailable masks; default no-op behavior; child/grandchild
inheritance; restoration after a synthetic build failure; and browser-mask rejection
before reading inputs or invoking WSL. Five builder-kit packaging tests also pass. PowerShell AST
parsing and Python ruff checks pass. No game/compiler build was launched for these
helper tests. The builder kit snapshots the source tree, so the new helper is
included once committed. Installer UI compilation is subject to local Inno Setup
availability; no installer was run.
