# E-TRIAGE-001 Fixed Weixin.dll Triage

## Sample Identity

- path: `/mnt/d/Program Files/Tencent/Weixin/4.1.15.13/Weixin.dll`
- sha256: `10f8e995453e2da46d4f2b5080cd6da1f13cc5147746adc119ceae38cb039de5`
- file_type: PE32+ executable DLL, x86-64, Windows console subsystem
- image_base: `0x180000000`
- entry_point: `0x1874f279c`
- quality: fixed local sample, offline static analysis

## Tooling

- Ghidra: `/home/user/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless`
- project: `/home/user/ghidra-projects/cfpay/Weixin_Options`
- GNU objdump: `/usr/bin/objdump`
- file: `/usr/bin/file`
- sha256sum: `/usr/bin/sha256sum`
- `rabin2` and `llvm-readobj` were not present; `objdump -p` supplied the required PE import/export evidence.

## Sections And Packing Indicators

- `.text` size: `0x0754b307`
- `.rdata` size: `0x03aec560`
- `.data` size: `0x008d8400`
- `.pdata` size: `0x005c48a0`
- Qt metadata sections and ordinary PE relocation/resource sections are present.
- No `UPX`, `VMProtect`, `Themida` or `ASPack` marker was found by the bounded string scan.
- quality: no obvious commodity packer marker; this does not prove absence of custom protection.

## Imports

- parser: `objdump -p`
- imported DLL descriptors: `KERNEL32.dll` only
- imported symbols counted: `333`
- representative dynamic-loading APIs: `LoadLibraryA/W`, `LoadLibraryExA/W`, `GetProcAddress`, `FreeLibrary`
- representative process/debug APIs: `CreateToolhelp32Snapshot`, `GetThreadContext`, `CheckRemoteDebuggerPresent`, `IsDebuggerPresent`
- quality: readable but abnormally clean for the observed feature set
- interpretation: the module likely resolves substantial functionality dynamically. The clean static IAT must not be used to deny UI, network, storage or other capabilities.

## Exports

- named exports counted: `117`
- exports are primarily `mmcronet` engine, request body, transfer and callback C++ symbols.
- representative exports: `mmcronet::Fetch`, `mmcronet::FetchWith`, `mmcronet::Engine`, `mmcronet::TransferHandle`.
- quality: readable and suitable as module identity evidence, but not directly tied to the target session preview renderer.

## Static Anchors

- `mmui::ChatSessionCell`
- `mmui::ChatSessionList`
- `mmui::ChatSessionCellViewModel`
- `mmui::ServiceSessionCell`
- `mmui::BrandSessionCell`
- `mmui::QueryChatSessionTopMsgResult`
- `unread_preview_msg_text_style`
- `unread_preview_sender_name_style`
- `OnSubscriptionSessionListUpdate`
- `ReadSubscriptionSessionSummary`
- `WriteSubscriptionSessionSummary`

## Triage Decision

The target payment amount appears in Weixin without opening the service-account conversation. Static work therefore moves to the global session-list summary and unread-preview binding path. The conversation-detail `ChatKfTemplateMsgItemView` path is excluded by dynamic zero-hit evidence after explicit session reselection.
