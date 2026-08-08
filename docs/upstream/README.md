# Upstream notes

Writeups aimed at the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
rather than at this port. They are kept here because they are the reason the
`thirdparty/rexglue-sdk` submodule points at a fork: each one describes a change
this port depends on that upstream does not carry yet.

| Note | Status |
|------|--------|
| [pr356-mac-fixes.md](pr356-mac-fixes.md) | Both fixes are in the pinned fork (`72eab46`, `ea57c2c`). Not yet offered upstream. |
| [codegen-drift.md](codegen-drift.md) | Draft issue, never filed. Describes a regression between SDK 0.8.1.28 and 0.8.1.93; the workaround it mentions is historical and no longer in use — the current pin is v0.9.3.1. Kept as a record of the symptom in case it resurfaces. |

Treat these as drafts, not as documentation of current behaviour.
