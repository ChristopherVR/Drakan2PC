# Single repository: Drakan2PC

Drakan2PC-dev (private) and Drakan2PC (public) were merged so that only Drakan2PC remains. `origin` now points to
`github.com/ChristopherVR/Drakan2PC`; the `public` remote and branch are gone and Drakan2PC-dev is archived (private,
not deleted).

## What happened

- The first attempt pushed the full development history to Drakan2PC. GitHub warned about
  `third_party/nvidia/dlss/nvngx_dlss.dll` (added in the dev history, later removed), which the pre-push audit had passed.
- Cause: `repository_guard.py --pre-push` excluded every object reachable from *any* remote-tracking ref
  (`--not --remotes`). Everything already on the private `origin` was therefore skipped when pushing to the public remote.
- With the audit fixed to exclude only the destination remote's refs (the hook now passes the remote name), the full
  history also showed a disc file listing (`analysis/iop/iso_listing.txt`), literal VU1 microcode words and old
  `patches/` content, all removed from the tree long ago.
- Drakan2PC `main` was then force-pushed as one squash commit of the audited tree on top of the earlier public snapshot
  history (`fa6bc11`), so none of that history is public any more. GitHub may keep unreferenced objects from the brief
  full-history push until GitHub Support runs a garbage collection.

## Rules that follow

- Push only to Drakan2PC. Never push the archived dev history to it.
- A full-history audit: `echo "refs/heads/main $(git rev-parse HEAD) refs/heads/main 0000000000000000000000000000000000000000" | python scripts/repository_guard.py --pre-push audit`
  (a remote name with no refs audits everything reachable).
