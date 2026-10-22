# E-DYNAMIC-001 Pre-login Session Preview Diagnostic

## Setup

- The production receipt observer was stopped before the diagnostic started.
- The diagnostic DLL was loaded into the validated `Weixin.exe` process before the native C++ login action.
- The diagnostic recorded counters and text shapes only. It did not persist source text, amounts, account data, payer data or transaction identifiers.

## Observation

After the native login action restored the main Weixin interface:

- `viewModelBind` reached `204`
- `serviceFamilyBind` reached `8`
- `updateWrapper` and `globalFamilyBind` remained `0`
- field `sessionModel + 0x160` was readable on all `204` binding calls
- field `sessionModel + 0x160` produced receipt, payment and amount-marker classifications
- field `sessionModel + 0x1b8` was readable but empty on all `204` binding calls
- text read failures remained `0`

## Finding

`Weixin.dll + 0x1c43ef0` is dynamically confirmed as a global session ViewModel binding path during login restoration. For this runtime state, `sessionModel + 0x160` carries the display text shape relevant to payment or receipt sessions, while `sessionModel + 0x1b8` does not carry the visible restored summary.

The earlier static assumption that `+0x1b8` was the strongest digest candidate is superseded by this dynamic evidence. A fresh receipt is still required to distinguish a session title or service name from a live receipt summary and to determine whether the amount is present in this binding path.

## Fresh Receipt Increment

The administrator confirmed a new payment at `16:13` while the same diagnostic remained active. At approximately `16:13:53`, the first correlated increment was:

- `viewModelBind`: `254 -> 258`
- `serviceFamilyBind`: `24 -> 25`
- receipt-keyword classification: `1 -> 2`
- payment-keyword, amount-marker and decimal-amount classifications: no correlated increment
- `sessionModel + 0x1b8`: remained empty
- text read failures: remained `0`

This confirms that `Weixin.dll + 0x1c43ef0` participates in the service-session update caused by a fresh receipt. It does not confirm that either directly sampled source field contains the receipt body or amount. The observed shape of `sessionModel + 0x160` is more consistent with a session or service title.

Static review of the same function shows that `FUN_181949b30(param_1 + 0x160, param_2)` constructs a nested display state before the direct `param_2 + 0x160` property assignment. The next dynamic target is therefore the output of `FUN_181949b30` or its final text setter, not another direct offset guess in `sessionModel`.

## Expanded Model Restore

After replacing the diagnostic DLL through a clean Weixin restart, the diagnostic was installed before the native login action. Restoring the main interface produced:

- `viewModelBind = 214`
- `serviceFamilyBind = 11`
- all fifteen statically confirmed string slots readable with zero failures
- rich-text vector readable with zero elements on all observed bindings
- `sessionModel + 0x48` classifications: receipt `4`, payment `4`, amount marker `6`, decimal amount `2`
- `sessionModel + 0x160` classifications: receipt `1`, payment `3`, amount marker `2`, decimal amount `0`

`sessionModel + 0x48` is the only restored field that simultaneously carries receipt, payment, amount-marker and two-decimal amount shapes. It is therefore the highest-confidence candidate for the complete session-row summary sentence. The empty rich-text vector excludes styled runs as the current list-summary carrier.

The next refinement is to classify `+0x48` specifically at the already identified service-session callsite, avoiding aggregate matches from unrelated payment sessions.

## Service Callsite Exclusion

The dedicated probe at `Weixin.dll + 0x48f9667` was deployed before another native login restoration. At `viewModelBind = 214`, this callsite had executed `11` times with zero read failures.

Its source-model `+0x48` field remained a three-byte value and produced zero receipt, payment, amount-marker and decimal-amount classifications. During the same run, aggregate `+0x48` classifications still reached receipt `4`, payment `4`, amount marker `6` and decimal amount `2`.

Therefore `+0x48f9667` participates in service-session refresh but does not directly carry the visible receipt summary. The amount-bearing model reaches `Weixin.dll + 0x1c43ef0` through another direct caller. The next diagnostic must bucket the entry hook by its stack return address.

## Amount-Bearing Caller

The entry diagnostic bucketed `sessionModel + 0x48` by six known direct-call return addresses plus one fallback bucket. Login restoration reached `210` binding calls with zero read failures.

The return-address bucket `Weixin.dll + 0x1c43924` accounted for `171` calls and contained all aggregate receipt-summary classifications:

- receipt keyword: `4`
- payment keyword: `4`
- amount marker: `6`
- decimal amount: `2`

No other fixed caller bucket or fallback bucket contained any of those four classifications. Static cross-references identify `+0x1c43924` as the return address after the call from `FUN_181c43510` to `FUN_181c43ef0`.

The highest-confidence live target is therefore `Weixin.dll + 0x1c43ef0`, restricted to return address `Weixin.dll + 0x1c43924`, with the complete summary text in source model field `+0x48`. One fresh receipt is required to verify synchronized live increments before this path replaces the production parser.

## Persistent ViewModel Tracking

The administrator confirmed that the visible row is one complete sentence containing payment, receipt, a two-decimal amount and a scene description. The real sentence and amount were not retained.

Static decompilation shows that `Weixin.dll + 0x1c43ef0` starts with `FUN_181949b30(viewModel + 0x160, sessionModel)`. The complete source display model is therefore embedded in the long-lived `ChatSessionCellViewModel`, and source field `sessionModel + 0x48` maps to `viewModel + 0x1a8`.

The diagnostic now registers a ViewModel only when all four summary classifications are present in the `+0x1c43924` caller bucket. It polls `viewModel + 0x1a8` every 250 milliseconds and retains only an in-process hash, byte length and aggregate change counters.

During a clean login restoration, the diagnostic observed `205` binding calls, retained the same `171` constructor-caller hits, registered two matching ViewModels and completed repeated reads with zero failures. The baseline counters were `changes=0` and `qualified=0`.

A fresh receipt is now required to determine whether the long-lived embedded model changes in place when the visible WeChat row updates. A synchronized increment in both counters will validate this path without requiring the receipt-assistant page to remain open.

## Fresh Receipt Result

Before the fresh payment, two matching ViewModels were active with `changes=0`, `qualified=0` and zero read failures.

After one new small payment, the persistent tracker reported:

- `changes`: `0 -> 2`
- `qualified`: `0 -> 2`
- read failures: `0`
- constructor caller `+0x1c43924`: unchanged at `171` hits

The source-field receipt, payment, amount-marker and decimal classifications also increased during the refresh. The unchanged constructor caller proves that the live receipt did not require a new `ChatSessionCellViewModel`; the already registered embedded display model changed in place.

This validates `ChatSessionCellViewModel + 0x1a8` as the live structured receipt-summary source. The production observer must parse the amount from this field and deduplicate multiple qualified transitions generated by one receipt update.

## Subsequent Receipt Caller Variation

The same diagnostic process remained active for a later small receipt. Around 17:00 local time, the aggregate binding count increased from `215` to `231`, and the service-family count increased from `11` to `13`.

The source `sessionModel + 0x48` receipt, payment, amount-marker and decimal classifications each increased once. The fixed constructor bucket `+0x1c43924` remained unchanged at `171` hits, while the fallback caller bucket gained one matching classification in all four categories.

The two previously registered ViewModels remained readable with zero read failures, but their `changes` and `qualified` counters remained at `2`. This means the summary field mapping is still valid, but restricting registration to the single `+0x1c43924` caller is incomplete. Production tracking must accept every qualified `+0x1c43ef0` binding source and maintain startup suppression plus normalized deduplication for newly bound objects.

## Post-Decode Append Call Chain

The unwind-enabled diagnostic was installed in the already logged-in validated Weixin process after the earlier diagnostic had been stopped and unloaded. A background refresh produced one structured-record append without a new payment or user interaction.

The event reported:

- direct return RVA: `+0x0000d23f`
- module-relative call chain: `+0x0000d23f`, `+0x00057772`, `+0x0194834b`, `+0x019746d0`, `+0x001d1ed4`, `+0x001d1c70`, `+0x00f4cbd0`, `+0x003892f8`, `+0x00389289`, `+0x0063b583`, `+0x01618218`, `+0x01617d02`, `+0x006978a3`, `+0x0062e75f`, `+0x0062e60b`, `+0x0062e20f`
- retained module frames: `16`
- walked frames: `32`
- unwind complete: `true`
- caller read failures, unwind failures and queue drops: `0`

The first frame remains the generic subscriber broadcaster. Static classification found a `0x300` record constructor and batch iterator, followed by generic asynchronous, timer and window-message frames. No network-response or deserialization boundary was established. This background event is retained as a comparison baseline only and must not be treated as proof of network origin.

## Fresh Payment Comparison

Before the administrator's next payment, the active append diagnostic reported `recordAppends=53`. At `2026-09-28T18:37:46+08:00`, the counter reached `55` after the payment was confirmed in Weixin.

Both new events retained the exact same sixteen-module-frame chain as the background baseline. Caller reads, x64 unwind operations and event queue delivery all remained failure-free.

This proves that a fresh receipt reaches the existing batch conversion and publication chain. It does not establish network provenance because the asynchronous boundary has already discarded the network parser's call stack. The chain is excluded as the production network hook.

## Privacy

No source strings or business values were retained. The evidence contains aggregate counters and boolean classifications only.
