# Research

Literature surveys, and the citations that the design documents one level up rest on.

These are kept separate for one reason: a design document should be readable as a design document.
When the evidence and the decision live in the same file, the file grows until nobody reads either —
`ACOUSTICS.md` reached three and a half times the length of `ARCHITECTURE.md`, which describes the
entire renderer, while describing a single phase that does not exist yet.

So the split is by role rather than by topic. A document up there says what is being built and why.
A document down here says how we know, and is where a reader goes when they want to check the claim
rather than act on it.

## What is here

| Document | Supports |
|----------|----------|
| [acoustics.md](acoustics.md) | [ACOUSTICS.md](../ACOUSTICS.md) — geometrical acoustics, the creature roster's audiograms, and the full reference list for Phase 5 |

## The rule these follow

Every citation is checked against its source before it is written down, and where a work is cited
for a specific number, that number is read out of the work rather than remembered. A fabricated
reference is worse than an omitted one: it is not merely wrong, it discredits everything beside it.

Anything that could not be verified is either left out or marked as unverified in place. Nothing
here is load-bearing for correctness — the code does not read these files — but they are what a
future contributor will use to decide whether a decision still holds, so they have to be honest
about how solid the ground under each one actually is.
