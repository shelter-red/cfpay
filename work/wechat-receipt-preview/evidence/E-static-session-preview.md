# E-STATIC-001 Global Session Preview Binding

## Anchors

The fixed sample contains the following session-list and preview anchors:

- `mmui::ChatSessionCell`
- `mmui::ChatSessionList`
- `mmui::ChatSessionCellViewModel`
- `mmui::ServiceSessionCell`
- `mmui::BrandSessionCell`
- `unread_preview_msg_text_style`
- `OnSubscriptionSessionListUpdate`

`FindWechatSessionPreviewRefs.java` found 18 string instances and 21 reconstructed RIP-relative references.

## ChatSessionCell

- class factory: `Weixin.dll + 0x17beb0`
- constructor selected for `mmui::ChatSessionCell`: `Weixin.dll + 0x09ac0f0`
- confirmed primary vtable: `0x188eb85c8`
- confirmed secondary vtable: `0x188eb8678`
- base primary vtable: `0x188eb8e28`
- base secondary vtable: `0x188eb8ed8`

The derived/base vtable comparison leaves four class-specific overrides. Their decompilation shows row height, state flags, style selection and interaction dispatch. They do not copy message text and are excluded as summary binding points.

## ChatSessionCellViewModel

The class uses Qt-style custom type metadata rather than a standard MSVC `TypeDescriptor` chain. Stored references to its conversion function recover a real method table at `0x189187238`.

- constructor: `Weixin.dll + 0x1c43510`
- metadata dispatcher: `Weixin.dll + 0x0892500`
- `qt_metacall` wrapper: `Weixin.dll + 0x0892840`
- structured bind/update function: `Weixin.dll + 0x1c43ef0`

The constructor initializes multiple reactive string, integer and boolean properties, then calls the bind function with the source session model.

## Structured Binding

`Weixin.dll + 0x1c43ef0(this, sessionModel)` performs the following display-model updates before the session row is rendered:

- copies the session identity returned from the source model into `this + 0x140`
- initializes nested session state at `this + 0x160`
- updates one text property from `sessionModel + 0x160`
- updates a second text property from `sessionModel + 0x1b8`, unless `sessionModel + 0x1d9 == 1`, in which case it supplies an empty string
- formats `sessionModel + 0x8c` through `Weixin.dll + 0x1c45a80` and writes the result into a separate text property
- updates unread count and multiple session state flags through dedicated fields and helper functions

The independent time conversion proves that `sessionModel + 0x1b8` is not the timestamp. Its conditional replacement with an empty string matches a hide-preview policy, making it the strongest session digest/preview-text candidate.

## Callers

The bind function has eight direct code paths, including:

- `Weixin.dll + 0x1c3d6aa`
- `Weixin.dll + 0x1c3dd7f`
- `Weixin.dll + 0x48f9667`
- `Weixin.dll + 0x48fa6f1`
- `Weixin.dll + 0x48fb561`
- `Weixin.dll + 0x490085d`
- `Weixin.dll + 0x4900fc4`

The constructor is also used by the adjacent session-list families at `+0x48f8ab0`, `+0x48f8e90`, `+0x48fa020`, `+0x48fae90`, `+0x49006f0` and related paths. This is a global session-list model, not a conversation-detail item.

## Dynamic Plan

Install one entry breakpoint at `Weixin.dll + 0x1c43ef0` before login. Record only:

- total binding count
- bounded string lengths for `sessionModel + 0x160` and `sessionModel + 0x1b8`
- fixed receipt/payment keyword shape and decimal-amount shape for the digest candidate
- hide-preview flag and caller return-address bucket
- read-failure counts

Do not persist source strings, account identifiers, amount values, payer data or transaction identifiers.

## Reproduction

Run the repository scripts against the existing read-only Ghidra project:

```bash
/home/user/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  /home/user/ghidra-projects/cfpay Weixin_Options \
  -process Weixin.dll -readOnly -noanalysis \
  -scriptPath /path/to/cfpay/tools/ghidra \
  -postScript FindWechatSessionPreviewRefs.java
```

Then run `InspectWechatSessionCellVtables.java`, `InspectWechatSessionTypeMetadata.java`, `DecompileWechatSessionPreviewCandidates.java` and `FindWechatSessionPreviewCallers.java` with the same project arguments.

## Display Model Copy Layout

Follow-up decompilation corrected the role of `FUN_181949b30`. It is a deep-copy routine for an approximately `0x290`-byte session display model, not the function that composes the visible summary sentence.

The copied model contains MSVC string objects at these offsets:

`+0x000`, `+0x048`, `+0x068`, `+0x0a8`, `+0x0c8`, `+0x100`, `+0x120`, `+0x140`, `+0x160`, `+0x198`, `+0x1b8`, `+0x208`, `+0x228`, `+0x248`, `+0x270`.

It also copies a vector range at `+0x1e8..+0x1f0`. The assignment routine advances by `0x28` bytes per element. Each element begins with a 32-byte MSVC string and carries eight additional bytes of style or metadata. This layout is consistent with a visible sentence represented as styled text runs, including a separately styled amount segment.

The administrator observed that the refreshed session row contains a complete sentence with payment, receipt, amount and scene-description components. No source sentence or amount value is retained in this evidence.

The next dynamic diagnostic therefore samples all statically confirmed string slots and the rich-text vector shape at the existing `Weixin.dll + 0x1c43ef0` entry. It records only lengths and boolean classifications.

## Upstream Source Trace

The reproducible `TraceWechatReceiptSummaryUpstream.java` script traversed two direct caller levels from the display-model constructor, both tail-call wrappers and the subscription-session update anchors. Ghidra 12.1.4 selected and decompiled 27 functions without modifying the sample.

Two wrappers tail-call the binding function:

- `FUN_181c3d540` jumps from `Weixin.dll + 0x1c3d6aa` to `+0x1c43ef0`.
- `FUN_1849006f0` jumps from `Weixin.dll + 0x490085d` to `+0x1c43ef0`.

Because a tail call preserves the wrapper caller's return address, either path is classified by the live entry diagnostic as a fallback caller rather than one of the six direct `CALL` return buckets. The service-session wrapper is called by `FUN_1849032c0` at `+0x4903674`; `FUN_184903110` conditionally invokes that batch refresh.

The larger service-session rebuild function `FUN_1848f7090` enumerates stored session objects and dispatches model construction through `FUN_1848f8ab0`, `FUN_1848f8e90`, `FUN_1848fa020` and `FUN_1848fae90`. Its upstream entry is not represented by an ordinary direct call reference, indicating a function-table or registered callback boundary. The next static stage must resolve that pointer table and its registration site before selecting a post-decode hook.

## Decoded-Network Boundary Exclusions

The follow-up callback-table and neighborhood scripts excluded several superficially upstream paths:

- `FUN_1848f0e70` is a generic session refresh dispatcher and calls `FUN_1848fd550`; it is not a decoded response callback.
- `FUN_184904ae0` only stores a shared object in state fields `+0x250/+0x258`.
- `FUN_18490b260`, `FUN_18490b5c0` and `FUN_18490b920` receive `mmcronet::TransferHandle` or Engine-associated objects. They switch transport handles and request refreshes but do not receive the decoded receipt business record.
- `FUN_185f26070` selects state from coordinates/current objects and invokes batch wrappers; it is not the network decoder delivery point.

The service-session storage path is now the strongest bridge to the post-decode object:

- `FUN_184902f70(serviceSession)` sets `serviceSession+0x130` to mark batch update start.
- `FUN_184903110(serviceSession)` clears the batch state and calls `FUN_1849032c0` to rebuild visible models.
- `FUN_1849032c0` iterates the vector described by `serviceSession+0x138..+0x140` using a `0x300`-byte record stride.

The next diagnostic must capture the service-session object at `FUN_184902f70`, then install an 8-byte write watchpoint on `serviceSession+0x140`. It records only the writer instruction RVA, thread identifier, sequence and hit count. It must not read or persist any record body. The real writer can then be decompiled and traced upward to the callback that receives the already-decoded network business object.

## Service-Session Vector Writer

Dynamic provenance corrected the original capture anchor. `FUN_184902f70` did not execute during a complete native login restoration and is excluded as a reliable object-capture point. The diagnostic instead captured the object at the confirmed vector consumer `FUN_1849032c0` and watched `serviceSession+0x140`.

Two post-write RVAs were observed:

- `+0x4903d2e`: the preceding instruction at `+0x4903d27` assigns `serviceSession+0x138` to `serviceSession+0x140`, clearing the vector after consumption.
- `+0x490d62a`: the preceding instruction at `+0x490d61f` adds `0x300` to `serviceSession+0x140`, completing one record append.

Ghidra identifies the append owner as `FUN_18490d5c0`. Its effective signature is:

```text
FUN_18490d5c0(callbackWrapper, decodedRecord, ..., ...)
callbackWrapper+0x08 -> serviceSession
decodedRecord        -> complete 0x300-byte source record
```

The function constructs the record at the current vector end, copies the nested object at `+0x290`, copies the state at `+0x2f8`, and advances the end pointer by `0x300`. This is the strongest current post-decode/pre-display structured-record entry.

The apparent data reference at `18bd3d668` is not a callback table. It is the function's PE `.pdata` runtime-function entry containing begin RVA `0x490d5c0`, end RVA `0x490d67a`, and unwind-info RVA `0x0a941b2c`. The remaining caller must therefore be recovered dynamically from the entry stack return address.

## Live Append Call-Chain Classification

`DecompileWechatPostDecodeCallChain.java` mapped all sixteen retained return RVAs from the background append event to their containing functions and preceding call instructions.

The lower business path is:

- `FUN_18000d0a0`: generic subscriber broadcaster; virtual callback slot `+0x10`
- `FUN_180057690`: generic guarded callback wrapper; virtual callback slot `+0x10`
- `FUN_181948200`: constructs a new `0x300` record from a main object, its `+0x290` nested object and its `+0x2f8` state, then publishes the completed record
- `FUN_181974590`: iterates a vector of record pointers and calls `FUN_181948200` for each non-null entry

The higher frames run through generic callback/task execution, timer processing and a Windows window-procedure path ending in `DefWindowProcW`. The anchor scan found no named network-response, transport decode, Protobuf or deserialization symbol in this captured path. `FUN_181974590` is stored in a callback operation table at `0x189106cd8`, constructed by `FUN_181946cd0`, and owned by the larger service constructor `FUN_18195b500`.

This background chain proves where a batch of already materialized records is converted into the shared `0x300` service-session format. It does not prove that the batch originated from a network response rather than local restoration or another background source. It is therefore not acceptable as the production network hook without a live-payment correlation that yields the same chain plus an independently established network decode boundary, or a distinct payment-only chain that exposes that boundary directly.

## NewSync Network Anchors

A fresh-payment comparison produced the same asynchronous batch-publication chain as the background baseline, so the search moved to the named Weixin NewSync protocol path.

The read-only Ghidra scan found these direct code references:

- `/cgi-bin/micromsg-bin/newsync` -> `FUN_18067fee0`, `FUN_182e2fbb0`, `FUN_182e324a0`
- `micromsg.NewSyncResponse` -> `FUN_181070420`
- `NewSyncStart` -> `FUN_182849c30`

`FUN_18067fee0` registers NewSync-related strings and source-file metadata. `FUN_181070420` only constructs the Protobuf type-name string. `FUN_182e324a0` builds a request carrying the NewSync URI. The larger `FUN_182e2fbb0` owns request execution, retry/error handling and a response-processing call to `FUN_182e30aa0`; it is the strongest current bridge into the decoded response path.

The alternative `id-it-subscriptionResponse` resource string has no code references and is excluded as a protocol entry. The next static stage must decompile callers and callees around `FUN_182e2fbb0`, `FUN_182e30aa0`, `FUN_18106e570` and `FUN_18106e990`, then locate the `NewSyncResponse` parse-complete and command-dispatch boundary before adding a live breakpoint.

## NewSyncResponse Protobuf Parser

The direct caller/callee expansion and table inspection recovered the generated `micromsg.NewSyncResponse` implementation. The earlier request/response classification was incorrect and is superseded below:

- constructor: `Weixin.dll + 0x106e570`
- destructor/reset path: `Weixin.dll + 0x106e990`
- field merge/copy path: `Weixin.dll + 0x106f690`
- parser: `Weixin.dll + 0x106fb10`
- serialized-size calculator: `Weixin.dll + 0x106f480`
- serializer: `Weixin.dll + 0x106f300`
- vtable: `0x188fc0d48`

The response parser occupies the response virtual table's parser slot and fills the response object before command dispatch. The corrected object is approximately `0x38` bytes, with the response presence bitmap at `+0x30`. The request parser remains a separate neighboring implementation and must not be used as the response capture point.

`FUN_182e2fbb0` constructs this response object, submits the NewSync request and passes the object into `FUN_182e30aa0`. The latter builds asynchronous state, waits for the operation result and copies the completed response wrapper; it is not itself the wire parser.

The corrected dynamic probe executes at `Weixin.dll + 0x106fb10`, samples the parse return at `+0x680fcc`, and separately observes the `micromsg.AddMsg` parser at `+0x1072ab0`. It records only response status, command counts and command IDs; AddMsg records only `messageType` and its presence bitmap. A Windows self-test produced NewSync command IDs `[5,17]` and AddMsg type `49`. The live diagnostic was installed in PID `4740` and captured `15` NewSync responses before clean shutdown, including multiple responses with `CmdID=5`, with zero read, unwind or queue failures. No live `addmsg_parse` event was captured. The most recent administrator payment is therefore not proven to have reached this parser, and the production hook remains blocked until a live payment or synchronized receipt produces a confirmed AddMsg event and its upstream network call chain.

## AddMsg Caller Follow-up

`FindWechatAddMsgCallers.java` enumerated the AddMsg constructor, parser and destructor references after the live probe failed to observe a parser hit. The references are dominated by the generated virtual table and function-table data; no ordinary direct call was sufficient to identify a business dispatch function. The NewSync follow-up scan added nearby response-list and generated-table helpers, but it did not establish a direct network-to-AddMsg caller.

This keeps the production boundary explicit: the AddMsg parser is a diagnostic lead only. The validated production candidate remains the structured session-summary binding at `Weixin.dll + 0x1c43ef0`, where the source summary field is `sessionModel + 0x48` and the persistent copied field is `ChatSessionCellViewModel + 0x1a8`.
