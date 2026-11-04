# Actor-scoped cinematic resources

Cinematic animation references must resolve in the actor model's database;
sound references resolve in the SSD's database. A global animation-id lookup
can bind a valid clip to the wrong skeleton when actors share source clip ids.

The sequence converter now accepts an actor-specific clip map. Supplying that
map disables the global fallback for an unverified actor. The pipeline uses
only source clips already written by compatible rig retargeting, or the clip
map of an imported own-rig model which survived final model pruning. It accepts
only source model-local (database0) clip references. Unmapped cross-database
clips remain explicit warnings; no arbitrary clip or rig is substituted.

The native retarget stage retains the source-to-target ids of clips it actually
wrote. Existing clip names, original action times, playback speed, transition
and channel fields are preserved. This adds no new clip bytes or global model
preload lists. A regression assigns the same source clip7 to targets41 and82
for two actors and rejects a third unverified actor, retaining time3.25.

Validation:18 sequence/retarget tests, formats typecheck and scoped Prettier
pass. The private regenerated candidate `build/scratch/ootf-animation-compatible.zip`
contains478 mapped animation actions across25 sequences. RV1.1/1.2a/1.3 have
83/81/34 respectively; packed action grammar and matching clip info/data entries
are audited. Candidate also contains the separate553-sound import and indexed
terrain; it is **not promoted** and has not passed live rig/actor validation.
In particular, a carried/current player may use a redirected avatar model and
must be verified against the actual live rig. Heap stability of the independent
indexed baseline remains the current blocker before broader resource testing.
