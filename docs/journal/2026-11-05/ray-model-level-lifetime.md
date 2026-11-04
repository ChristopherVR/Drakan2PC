# Native ray model lifetime across levels

The character-artifact review found an independently reproducible DXR cache
defect. Native mesh identity selected both attributes and a BLAS, but only the
attribute cache checked the current immutable mesh bytes. The BLAS cache skipped
all built identities. Reusing an identity for different geometry could therefore
pair new normals/material attributes with an old acceleration structure.

DXR now retains a separate source snapshot for each completed BLAS and validates
it before reuse. Attribute packing cannot overwrite this proof. Changed geometry
is rebuilt through the existing queue-ordered allocation/refit paths. Native
capture normally hashes mesh geometry into identity, so this fix closes a real
cache-contract hole without establishing that an ordinary hash collision caused
the reported character artifact.

The native capture resets resource/template state when its level changes, but
ray-model positional welds and acceleration-structure pools previously retained
their old namespace. A topology key deliberately omits posed positions; an equal
key and triangle count cannot prove that another level has the same positional
seams. DXR now clears native model, weld, deformable, material and alpha-coverage
state when a nonnull model snapshot changes level. Old BLAS references survive
in the next acquired frame's retirement list until its completion fence. A null
transient model snapshot does not establish a new level; a nonnull level zero
does. Static resident geometry keeps its separate version/lifetime rules.

Focused GPU regressions publish changed geometry under the same continuously
present identity, exercise deformable topology and a level change, and restore
the original image. A second fixture changes positional seams while preserving
topology and triangle count across levels. Its directionally lit reflected image
must match an independent per-mesh weld reference. Build and hardware results
are recorded by the integration run. These fixtures do not by themselves prove
the cause or resolution of the user's live Rynn transition artifact.
