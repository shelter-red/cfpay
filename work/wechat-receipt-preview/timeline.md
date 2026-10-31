# Timeline (append-only)

## 2026-09-28T14:41:47+08:00 | lead | init
- action: case-init
- command_or_ref: skills/scripts/case-init.sh
- result_summary: case directory created; scope ready_for_act=true
- artifacts: [scope.md, workitems.md]
- evidence_ids: []
- decision_delta: [case_initialized]
- carry_forward_refs: [scope.md]
- next: open PRIMARY SKILL.md and ACT within scope

## 2026-09-28T15:12:00+08:00 | cre | triage
- action: fixed sample triage and stale diagnostic cleanup
- command_or_ref: file, sha256sum, objdump, Ghidra headless, native stop entry
- result_summary: fixed x64 PE DLL identified; imports and exports recorded; KfTemplate diagnostic hardware breakpoints cleared while Weixin remained running
- artifacts: [evidence/E-triage.md, ../../docs/integration-journal.md]
- evidence_ids: [E-TRIAGE-001]
- decision_delta: [phase=triage->static, target=session_list_preview_binding, kf_template_path=excluded]
- carry_forward_refs: [scope.md, evidence/E-triage.md, ../../docs/integration-journal.md]
- next: locate ChatSessionCell and unread preview structured binding functions with Ghidra

## 2026-09-28T16:05:00+08:00 | cre | static
- action: recover ChatSessionCell and ChatSessionCellViewModel binding path
- command_or_ref: Ghidra headless scripts under tools/ghidra
- result_summary: global ViewModel binding found at Weixin.dll+0x1c43ef0; source model+0x1b8 feeds the summary text property while +0x8c is formatted separately as time
- artifacts: [evidence/E-static-session-preview.md, ../../tools/ghidra/FindWechatSessionPreviewRefs.java, ../../tools/ghidra/InspectWechatSessionTypeMetadata.java]
- evidence_ids: [E-STATIC-001]
- decision_delta: [target=Weixin.dll+0x1c43ef0, candidate_digest_field=sessionModel+0x1b8, phase=static->dynamic]
- carry_forward_refs: [scope.md, evidence/E-triage.md, evidence/E-static-session-preview.md]
- next: install a privacy-preserving entry diagnostic before login and verify restored session rows

## 2026-09-28T17:05:00+08:00 | cre | dynamic-preparation
- action: implement privacy-preserving session preview diagnostic and invalidate non-compliant live run
- command_or_ref: native/wechat-observer/hardware_observer.cpp; commits d259934 and b102c07
- result_summary: diagnostic build and harness passed; live counters produced after a WSL-originated Windows launch were downgraded to leads because that execution path can trigger security verification
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [dynamic_evidence=invalid, windows_execution=installed_native_or_manual_local_only, wsl_windows_invocation=forbidden]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: provide a Windows-native local start path, then repeat pre-login injection without WSL invocation

## 2026-09-28T17:32:00+08:00 | cre | native-field-test
- action: add Windows-native button, QR and matched-payment field tests
- command_or_ref: native/wechat-observer/wechat_monitor.cpp; native/wechat-observer/wechat_login_assistant.cpp
- result_summary: native build passed; field tests now run inside the single monitor EXE and require Worker matched=true for payment success
- artifacts: [../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/wechat_login_assistant.cpp, ../../docs/wechat-native-observer.md]
- evidence_ids: []
- decision_delta: [windows_execution=native_manual_start, qr_test=administrator_scan_gate, payment_test=administrator_payment_gate]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: stage the portable package in Windows local storage and ask the administrator to start it from Windows

## 2026-09-28T17:36:00+08:00 | cre | native-staging
- action: stage verified portable field-test binaries on the Windows local filesystem
- command_or_ref: dist/windows-wechat-monitor; %LOCALAPPDATA%/CFPay/WeChatFieldTest-0.8.1
- result_summary: auto-sequence binaries copied with matching SHA-256 values; no config or secret copied and no Windows process invoked
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [package=ready_for_windows_manual_start, wsl_windows_invocation=none]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: administrator exits the old monitor and starts the local portable EXE; agent then validates button login from native logs and Worker state

## 2026-09-28T15:41:00+08:00 | cre | native-live-validation
- action: run the Windows-local portable EXE directly and validate button login
- command_or_ref: cfpay-wechat-monitor.exe --field-test-sequence; no PowerShell intermediary
- result_summary: button click reached LOGGED_IN and HOOK_ACTIVE; sequence switched to QR login and Worker accepted QR_READY without persisting the payload
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [button_login=validated, qr_login=waiting_for_administrator_scan, payment=not_started]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: administrator scans the Worker-rendered QR; validate logged-in hook health and arm matched-payment test

## 2026-09-28T15:44:00+08:00 | cre | qr-and-payment-gate
- action: validate QR login and create a fresh payment test order
- command_or_ref: monitor native logs; HTTPS /mapi.php; explorer.exe direct launch
- result_summary: QR login reached LOGGED_IN and HOOK_ACTIVE; payment test armed and a fresh payment page opened without PowerShell
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [qr_login=validated, payment=waiting_for_administrator]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: administrator completes payment; validate structured hook capture, Worker matched=true and order confirmation

## 2026-09-28T15:53:00+08:00 | cre | payment-path-exclusion
- action: validate a real payment against the current receipt parser hook
- command_or_ref: Windows native field-test monitor logs; administrator-confirmed Weixin UI receipt
- result_summary: Weixin displayed the receipt while parser entry and return counters stayed at zero; no payment event reached Worker and matched=true was not observed
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [parser_0x2293980=excluded_for_main_ui_receipt, target=session_preview_0x1c43ef0, payment_test=failed]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: build and run the privacy-preserving session preview diagnostic without overlapping hardware breakpoints

## 2026-09-28T16:00:00+08:00 | cre | diagnostic-host-mode
- action: add a restricted native host mode for the session preview diagnostic
- command_or_ref: native/wechat-observer/wechat_host.cpp; MinGW build; targeted Vitest suite
- result_summary: diagnostic lifecycle is isolated by module name and restricted to the validated Weixin target; Windows x64 build and 33 targeted tests passed
- artifacts: [../../native/wechat-observer/wechat_host.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [diagnostic_host=ready, production_and_diagnostic_breakpoints=mutually_exclusive]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: stage the native diagnostic package, stop production breakpoints and validate existing session rows

## 2026-09-28T16:02:00+08:00 | cre | diagnostic-staging
- action: stage the verified native diagnostic package on the Windows local filesystem
- command_or_ref: dist/wechat-hook-harness; %LOCALAPPDATA%/CFPay/WeChatSessionDiagnostic-0.8.1
- result_summary: monitor EXE and session preview diagnostic DLL were copied with matching SHA-256 values; no config, secret or business data was copied
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [diagnostic_package=ready, live_process_state=unchanged]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: stop production breakpoints and start the diagnostic against the same logged-in Weixin process

## 2026-09-28T16:05:00+08:00 | cre | dynamic-session-binding
- action: install the session preview diagnostic before native login and observe restored session rows
- command_or_ref: --run-weixin-diagnostic; --self-test-login-ui direct; E-DYNAMIC-001
- result_summary: global ViewModel binding reached 204 calls with zero read failures; +0x160 contained receipt/payment/amount shapes while +0x1b8 remained empty
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [session_binding=validated, primary_text_field=+0x160, +0x1b8=superseded]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: trigger one fresh receipt while the diagnostic remains active and compare incremental counters

## 2026-09-28T16:08:00+08:00 | cre | fresh-receipt-gate
- action: create and open a fresh payment order while the session diagnostic remains active
- command_or_ref: HTTPS /mapi.php; explorer.exe direct launch
- result_summary: a new payment page was opened without PowerShell and without persisting order or payment values
- artifacts: [../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [fresh_receipt=waiting_for_administrator, diagnostic=active]
- carry_forward_refs: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- next: administrator completes payment; compare privacy-preserving incremental counters

## 2026-09-28T16:13:53+08:00 | cre | fresh-receipt-observation
- action: correlate a confirmed fresh receipt with session preview diagnostic counters
- command_or_ref: active native diagnostic status stream; E-DYNAMIC-001
- result_summary: viewModel binding and service-family counters increased with a receipt-keyword shape, but amount and decimal shapes did not; direct +0x160/+0x1b8 fields are not the receipt amount body
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [session_binding_receipt_update=validated, direct_source_fields=excluded_for_amount_body, target=FUN_181949b30_output]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: statically recover FUN_181949b30 output layout and instrument its privacy-preserving final text path

## 2026-09-28T16:29:00+08:00 | cre | display-model-layout
- action: recover the full copied session display model and expand privacy-preserving shape diagnostics
- command_or_ref: InspectWechatSessionPreviewTextBuilder.java; native/wechat-observer/hardware_observer.cpp
- result_summary: FUN_181949b30 is a 0x290-byte deep-copy routine with fifteen string slots and a 0x28-byte rich-text run vector; the existing binding breakpoint now classifies all fields without retaining source text
- artifacts: [evidence/E-static-session-preview.md, ../../tools/ghidra/InspectWechatSessionPreviewTextBuilder.java, ../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [FUN_181949b30=display_model_copy, candidate=all_string_slots_and_rich_runs, new_payment_required=false]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: replace the local diagnostic DLL and identify the restored-session amount-bearing field or rich-text run

## 2026-09-28T16:34:00+08:00 | cre | restored-summary-field
- action: deploy the expanded diagnostic before native login and classify the restored display model
- command_or_ref: --stop-weixin-diagnostic; --self-test-login-restart; --run-weixin-diagnostic; --self-test-login-ui direct
- result_summary: model+0x48 was the only field with receipt, payment, amount-marker and two-decimal amount shapes; model+0x160 had no decimal amount and the rich-text vector was empty
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [primary_summary_field=sessionModel+0x48, rich_text_vector=excluded_for_session_row, new_payment_required=false]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: classify model+0x48 at the service-session callsite to exclude unrelated payment conversations

## 2026-09-28T16:38:00+08:00 | cre | service-summary-diagnostic
- action: add service-family-specific classification for sessionModel+0x48
- command_or_ref: native/wechat-observer/hardware_observer.cpp; MinGW build; targeted Vitest suite
- result_summary: the existing service callsite breakpoint now reports a separate serviceSummary shape without adding breakpoints or retaining source text; build and 28 tests passed
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [service_summary_probe=ready, hardware_breakpoint_count=unchanged]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: deploy the diagnostic through a clean Weixin restart and validate restored service-session summary shapes

## 2026-09-28T16:42:00+08:00 | cre | service-callsite-exclusion
- action: validate model+0x48 specifically at the service-family callsite during login restoration
- command_or_ref: --stop-weixin-diagnostic; --self-test-login-restart; --run-weixin-diagnostic; --self-test-login-ui direct
- result_summary: the service callsite produced only a three-byte +0x48 field with no receipt or amount shape, while aggregate +0x48 still contained decimal amounts
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [callsite_0x48f9667=excluded_for_summary_body, target=entry_return_address_buckets]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: enumerate every direct caller return address and bucket +0x48 shapes at the entry hook

## 2026-09-28T16:46:00+08:00 | cre | caller-bucket-diagnostic
- action: bucket sessionModel+0x48 shapes by the binding entry return address
- command_or_ref: FindWechatSessionPreviewCallers.java; native/wechat-observer/hardware_observer.cpp
- result_summary: six direct CALL return addresses and one fallback bucket are now classified at the existing entry breakpoint; build and 28 tests passed
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [caller_buckets=ready, hardware_breakpoint_count=unchanged]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: deploy through a clean restart and identify the amount-bearing caller during login restoration

## 2026-09-28T16:50:00+08:00 | cre | amount-caller-identified
- action: validate caller-bucket shapes during native login restoration
- command_or_ref: --run-weixin-diagnostic; --self-test-login-ui direct; E-DYNAMIC-001
- result_summary: return address Weixin.dll+0x1c43924 exclusively contained all receipt, payment, amount-marker and decimal-amount shapes from model+0x48
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [target_entry=+0x1c43ef0, target_return=+0x1c43924, summary_field=+0x48, fresh_receipt_required=true]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: create one fresh test order and verify synchronized increments in the identified caller bucket

## 2026-09-28T16:58:30+08:00 | cre | persistent-summary-tracking-ready
- action: map source summary into the persistent ViewModel and add privacy-preserving change tracking
- command_or_ref: DecompileWechatSessionPreviewCandidates.java; MinGW build; four targeted Vitest files; controlled native login restoration
- result_summary: source model+0x48 maps to ViewModel+0x1a8; two matching ViewModels were registered after login restoration with zero read failures and zero baseline changes
- artifacts: [evidence/E-dynamic-session-preview.md, ../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [persistent_summary=ViewModel+0x1a8, tracked_viewmodels=2, fresh_receipt_required=true]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: pause for one fresh payment and verify tracked change plus qualified-shape increments

## 2026-09-28T17:00:19+08:00 | cre | persistent-summary-live-validated
- action: compare persistent ViewModel counters before and after one fresh receipt
- command_or_ref: live diagnostic session; E-DYNAMIC-001
- result_summary: one fresh receipt changed both aggregate counters from zero to two with zero read failures while the constructor caller count remained unchanged
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [live_summary_source=ViewModel+0x1a8, constructor_reentry_required=false, production_dedup_required=true]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: replace the production receipt parser with the validated display-model tracker and add normalized event deduplication

## 2026-09-28T17:05:00+08:00 | cre | subsequent-receipt-caller-variation
- action: compare caller and persistent-ViewModel counters after a later fresh receipt
- command_or_ref: live diagnostic session; E-DYNAMIC-001
- result_summary: the receipt shape reached the binding entry through the fallback caller bucket while the two constructor-filtered ViewModels remained unchanged and readable
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [single_caller_registration=incomplete, qualified_binding_sources=all, startup_suppression_required=true]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: broaden production registration to every qualified binding and preserve normalized short-window deduplication

## 2026-09-28T17:18:00+08:00 | cre | upstream-source-trace
- action: trace the receipt summary from display binding toward decoded subscription-session updates
- command_or_ref: TraceWechatReceiptSummaryUpstream.java; Ghidra 12.1.4 headless project Weixin_Options
- result_summary: both fallback sources are tail-call wrappers; the service path runs through FUN_1849032c0 and FUN_1849006f0, while its larger refresh entry is reached through a function-table or registered callback boundary
- artifacts: [../../tools/ghidra/TraceWechatReceiptSummaryUpstream.java, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [target_stage=post_decode_pre_viewmodel, fallback_callers=tail_wrappers, next_boundary=function_table_registration]
- carry_forward_refs: [scope.md, evidence/E-triage.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: resolve the service-session refresh function pointer table and trace its registration back to the decoded response callback

## 2026-09-28T17:31:00+08:00 | cre | decoded-boundary-exclusions
- action: exclude transport-handle, generic refresh and state-selection callbacks from the decoded receipt path
- command_or_ref: TraceWechatDecodedNetworkObject.java; TraceWechatNetworkTaskRegistration.java; TraceWechatDecodedServiceDelivery.java; DecompileWechatServiceRefreshCallbacks.java
- result_summary: transport callbacks only switch mmcronet handles, while the service-session object owns a 0x300-record vector whose +0x140 end pointer is the next dynamic provenance point
- artifacts: [evidence/E-static-session-preview.md, ../../docs/integration-journal.md, route-scope.md]
- evidence_ids: [E-STATIC-001]
- decision_delta: [transport_handle_callbacks=excluded, generic_refresh=excluded, next_probe=serviceSession+0x140_write]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: capture the service-session object at FUN_184902f70 and record only the actual writer instruction RVA for +0x140

## 2026-09-28T17:39:00+08:00 | cre | vector-write-provenance-diagnostic
- action: implement and self-test a two-stage hardware-breakpoint diagnostic for the service-session vector end pointer
- command_or_ref: network_write_observer.cpp; cfpay-hook-test-target.exe; x86_64-w64-mingw32-objdump
- result_summary: one batch-arm event and two +0x140 writes were captured; the data breakpoint reported the instruction immediately after the real write, establishing postWriteRva semantics
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [network_provenance_probe=ready, post_write_address_semantics=validated, payload_reading=disabled]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: cleanly stop the old session diagnostic, deploy the network provenance diagnostic, and recover the real writer from the first live postWriteRva

## 2026-09-28T17:49:00+08:00 | cre | batch-start-anchor-excluded
- action: deploy before native login and validate whether FUN_184902f70 can capture the service-session object
- command_or_ref: cfpay-hook-network-diagnostic.dll; --self-test-login-restart; --self-test-login-ui direct
- result_summary: Weixin restored the full main process and child processes while the candidate remained at zero hits, so the batch-start anchor was excluded
- artifacts: [../../docs/integration-journal.md, ../../native/wechat-observer/network_write_observer.cpp]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [FUN_184902f70=excluded_as_capture_anchor, capture_anchor=FUN_1849032c0, writer_watch=serviceSession+0x140]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: capture the service-session object at the confirmed vector consumer and wait for the next upstream vector write

## 2026-09-28T17:54:00+08:00 | cre | structured-record-appender-identified
- action: classify live +0x140 writes and decompile the owning functions
- command_or_ref: cfpay-hook-network-diagnostic.dll; DecompileWechatVectorWriters.java; TraceWechatVectorAppendRegistration.java
- result_summary: +0x4903d27 clears the vector, while FUN_18490d5c0 appends one complete 0x300-byte record and advances the end pointer at +0x490d61f
- artifacts: [evidence/E-static-session-preview.md, ../../docs/integration-journal.md, ../../tools/ghidra/DecompileWechatVectorWriters.java, ../../tools/ghidra/TraceWechatVectorAppendRegistration.java]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [record_append_entry=FUN_18490d5c0, decoded_record_arg=param_2, pdata_reference=not_callback_registration]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: hook FUN_18490d5c0 directly and recover its indirect caller return address without reading the decoded record body

## 2026-09-28T18:00:00+08:00 | cre | direct-record-append-probe
- action: replace the consumer-plus-watchpoint diagnostic with a single entry breakpoint at FUN_18490d5c0
- command_or_ref: native/wechat-observer/network_write_observer.cpp
- result_summary: the probe now records only the entry return address and counters; decoded record memory remains unread
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [breakpoint_count=1, event=decoded_record_append, next_evidence=callerRva]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: validate callerRva during native login restoration, decompile the dispatcher, then pause only for the final fresh-payment confirmation

## 2026-09-28T18:06:00+08:00 | cre | generic-dispatcher-excluded
- action: capture the direct append caller and classify its owning function
- command_or_ref: active native diagnostic; DecompileWechatRecordAppendDispatcher.java
- result_summary: both appends returned to Weixin.dll+0x0000d23f; the indirect call at +0x0000d23c belongs to a generic subscriber broadcaster with many tail-call wrappers
- artifacts: [../../tools/ghidra/DecompileWechatRecordAppendDispatcher.java, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [FUN_18000d0a0=generic_subscriber_broadcaster, single_return_address=insufficient, next_probe=bounded_x64_unwind]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: unwind module-relative return addresses from the append-entry context and identify the first network decode-specific caller

## 2026-09-28T18:13:00+08:00 | cre | bounded-x64-unwind-ready
- action: add bounded Windows x64 unwinding to the record-append diagnostic and validate it in the native test target
- command_or_ref: RtlLookupFunctionEntry; RtlVirtualUnwind; cfpay-hook-test-target.exe
- result_summary: both self-test appends produced complete five-RVA target-module call chains matching the disassembled two-layer test wrappers
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../native/wechat-observer/test_target.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [bounded_unwind=validated, raw_stack_dump=disabled, next_evidence=live_callChainRvas]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: stop the active live diagnostic, deploy the unwind-enabled DLL, and capture the first caller above the generic broadcaster

## 2026-09-28T18:16:00+08:00 | cre | live-post-decode-call-chain
- action: deploy the unwind-enabled diagnostic and capture a background structured-record append
- command_or_ref: --stop-weixin-network-diagnostic; --run-weixin-network-diagnostic; E-DYNAMIC-001
- result_summary: one natural refresh produced a complete zero-failure sixteen-RVA Weixin call chain above the generic broadcaster without requiring a payment
- artifacts: [evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [live_call_chain=captured, generic_dispatcher_bypassed, network_decoder=not_yet_proven]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: map every retained return RVA to its containing function and identify the network response parser or deserializer publisher

## 2026-09-28T18:25:00+08:00 | cre | background-source-not-proven
- action: classify every retained frame and trace the batch-consumer callback registration
- command_or_ref: DecompileWechatPostDecodeCallChain.java; TraceWechatPostDecodeCallbackRegistration.java; InspectWechatPostDecodeAnchors.java
- result_summary: the chain reaches a 0x300 record constructor and batch iterator but then continues through generic async, timer and window-message dispatch without a proven network decode boundary
- artifacts: [evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md, ../../tools/ghidra/DecompileWechatPostDecodeCallChain.java, ../../tools/ghidra/TraceWechatPostDecodeCallbackRegistration.java, ../../tools/ghidra/InspectWechatPostDecodeAnchors.java, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [background_chain=network_origin_unproven, production_hook=blocked_on_live_payment_chain, diagnostic=active]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: pause for one administrator payment and compare the resulting live call chain against the background baseline

## 2026-09-28T18:37:46+08:00 | cre | fresh-payment-chain-excluded
- action: compare a confirmed fresh receipt against the background structured-record append chain
- command_or_ref: active native network diagnostic; E-DYNAMIC-001
- result_summary: recordAppends increased from 53 to 55, but both new events retained the exact background call chain with zero caller, unwind or queue failures
- artifacts: [evidence/E-dynamic-session-preview.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [fresh_receipt_reaches_batch_publisher=true, batch_publisher_network_origin=excluded, target=NewSyncResponse_parse_and_dispatch]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: trace named NewSync network and Protobuf anchors instead of following the asynchronous batch stack

## 2026-09-28T18:39:00+08:00 | cre | newsync-network-anchors
- action: locate direct code references to the NewSync URI, response type and phase markers
- command_or_ref: TraceWechatNewSyncNetworkPath.java; TraceWechatSubscriptionResponse.java; DecompileWechatServiceCallbacks.java
- result_summary: five NewSync anchor functions were recovered; FUN_182e2fbb0 is the strongest request/response owner, while the subscriptionResponse resource has no code references
- artifacts: [../../tools/ghidra/TraceWechatNewSyncNetworkPath.java, ../../tools/ghidra/TraceWechatSubscriptionResponse.java, ../../tools/ghidra/DecompileWechatServiceCallbacks.java, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [newsync_uri_functions=3, response_type_constructor=FUN_181070420, response_owner_candidate=FUN_182e2fbb0, resource_anchor_excluded=id-it-subscriptionResponse]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: expand callers and callees around FUN_182e2fbb0 and FUN_182e30aa0 to find the parse-complete command dispatcher

## 2026-09-28T18:48:00+08:00 | cre | newsync-response-parser
- action: expand the NewSync request owner and inspect the generated NewSyncResponse function tables
- command_or_ref: TraceWechatNewSyncResponseDispatch.java; InspectWechatNewSyncResponseTables.java
- result_summary: FUN_18106eb90 is the generated Protobuf parser at vtable offset +0x38; it fills fields 1 through 9 and updates the response presence bitmap
- artifacts: [../../tools/ghidra/TraceWechatNewSyncResponseDispatch.java, ../../tools/ghidra/InspectWechatNewSyncResponseTables.java, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001]
- decision_delta: [newsync_response_constructor=+0x106e570, protobuf_parser=+0x106eb90, parser_vtable_offset=+0x38, async_result_extractor=FUN_182e30aa0]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: replace the old append diagnostic with a bounded call-chain probe at the NewSyncResponse parser entry

## 2026-09-28T18:52:00+08:00 | cre | newsync-parser-probe-ready
- action: retarget the isolated network diagnostic to the NewSyncResponse Protobuf parser
- command_or_ref: network_write_observer.cpp; cfpay-hook-test-target.exe; InspectWechatNewSyncResponseTables.java
- result_summary: two self-test parser hits produced complete five-RVA module call chains; stop cleanup succeeded and the diagnostic DLL retained only system dependencies
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../native/wechat-observer/test_target.cpp, ../../tools/ghidra/InspectWechatNewSyncResponseTables.java, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [diagnostic_breakpoint=+0x106eb90, event=newsync_response_parse, health_interval_seconds=5, payload_reading=disabled]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: stop the old live diagnostic, deploy the parser probe and capture the first natural NewSync call chain

## 2026-09-28T19:03:00+08:00 | cre | newsync-thread-coverage-race
- action: investigate a zero-hit parser probe after a successful native login restoration
- command_or_ref: active NEWSYNC_PARSER_ACTIVE stream; Windows process tree; network_write_observer.cpp
- result_summary: all pre-login threads were covered, but the first NewSync parse could run on a newly created login network thread before the 250ms coverage scan
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [pre_first_hit_coverage_ms=25, post_first_hit_coverage_ms=250, parser_candidate=unchanged]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: redeploy before login and repeat the native login synchronization test

## 2026-09-28T19:36:37+08:00 | cre | newsync-response-correction-and-addmsg-probe
- action: stop the old diagnostic, rebuild the corrected network probe, run the self-test, and deploy it to the current main Weixin process
- command_or_ref: `./tools/build-wechat-hook-harness.sh`; `cfpay-wechat-monitor.exe --internal-host --run-weixin-network-diagnostic 4740 ...`
- result_summary: self-test captured two NewSync responses with `commandIds=[5,17]` and two AddMsg parses with `addMsgType=49`; live installation succeeded and captured `15` NewSync responses, including multiple `CmdID=5` batches, with zero read/unwind/queue failures, but no live AddMsg parse
- artifacts: [../../native/wechat-observer/network_write_observer.cpp, ../../native/wechat-observer/test_target.cpp, ../../tools/ghidra/InspectWechatAddMsgTables.java, ../../tools/ghidra/TraceWechatAddMsgNetworkPath.java, ../../tools/ghidra/TraceWechatNewSyncParserCallChain.java, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md, evidence/E-static-session-preview.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [request_response_parser_confusion=corrected, response_parser=+0x106fb10, newsync_return_probe=+0x680fcc, addmsg_parser=+0x1072ab0, live_newsync_observed=true, live_addmsg_observed=false, production_hook=still_blocked]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md, ../../docs/wechat-native-observer.md]
- next: keep the corrected probe active through a fresh synchronized receipt, correlate `CmdID=5` with `addmsg_parse`, then recover the network-side caller chain before changing the production observer

## 2026-09-28T19:40:36+08:00 | cre | post-payment-replay-check
- action: after the administrator reported another payment, reinstall the corrected network diagnostic into the existing main Weixin process and observe without restarting Weixin
- command_or_ref: `cfpay-wechat-monitor.exe --internal-host --run-weixin-network-diagnostic 4740 ...`; `--stop-weixin-network-diagnostic 4740`
- result_summary: the replay window captured `3` NewSync responses, one with `CmdID=5`, complete `16`-frame chains and zero read/unwind/queue failures; `addmsg_parse=0`; local state remained `pending=0`, `delivered=0`
- artifacts: [../../docs/integration-journal.md, ../../docs/wechat-native-observer.md, evidence/E-static-session-preview.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [post_payment_newsync_seen=true, post_payment_cmd5_seen=true, post_payment_addmsg_seen=false, local_payment_delivery_seen=false]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, ../../docs/integration-journal.md]
- next: install the probe first, then pause for a fresh administrator payment and correlate the exact NewSync/AddMsg event window

## 2026-09-28T20:00:00+08:00 | cre | addmsg-caller-static-followup
- action: enumerate AddMsg lifecycle references and extend NewSync response dispatch targets
- command_or_ref: FindWechatAddMsgCallers.java; TraceWechatNewSyncResponseDispatch.java; Ghidra headless outputs
- result_summary: AddMsg constructor/parser/destructor references are indirect through virtual or generated tables; no ordinary direct business caller was proven, so the existing AddMsg parser is not promoted to the production payment hook
- artifacts: [../../tools/ghidra/FindWechatAddMsgCallers.java, ../../tools/ghidra/TraceWechatNewSyncResponseDispatch.java, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [addmsg_direct_caller=unproven, production_candidate=session_preview_0x1c43ef0]
- carry_forward_refs: [scope.md, evidence/E-static-session-preview.md, evidence/E-dynamic-session-preview.md]
- next: replace the obsolete production receipt breakpoint with the validated session-summary observer and add guarded amount parsing/deduplication

## 2026-09-28T20:38:00+08:00 | cre | production-session-summary-hook
- action: switch the production receipt breakpoint to the validated session-summary binding and deploy it to the live Weixin process
- command_or_ref: native/wechat-observer/hardware_observer.cpp; ./tools/build-wechat-hook-harness.sh; %LOCALAPPDATA%/CFPay/WeChatMonitor
- result_summary: observer DLL 0x00070000 loaded into PID 4740 with hookOffset 0x1c43ef0; Worker reports LOGGED_IN and HEALTHY/HOOK_ACTIVE with new summaryChanges and summaryPayments counters
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [production_receipt_entry=+0x1c43ef0, parser_0x2293980=removed_from_production, capture_paths=binding_entry_plus_viewmodel_poll, startup_suppression_ms=30000, duplicate_window_ms=8000]
- carry_forward_refs: [scope.md, evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- next: pause for one fresh administrator payment and verify local queue delivery plus Worker order matching

## 2026-09-28T21:01:00+08:00 | cre | live-capture-verified
- action: capture two real 0.01 receipts with the production session-summary observer and verify Worker matching
- command_or_ref: %LOCALAPPDATA%/CFPay/wechat-monitor.log; /mapi.php order wx-live-1790600355-1612; /api.php?act=order
- result_summary: capture-to-queue latency 2-5ms; second receipt uploaded HTTP 200, order became PAID and the epay query returned status=1; the first receipt stayed unmatched because no live order existed
- artifacts: [../../docs/integration-journal.md, ../../native/wechat-observer/hardware_observer.cpp]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [session_summary_chain=production_verified, duplicate_event_at_plus_91s=defect]
- carry_forward_refs: [scope.md, evidence/E-dynamic-session-preview.md, ../../docs/integration-journal.md]
- next: suppress duplicate emissions and re-verify one receipt produces exactly one event

## 2026-09-28T21:22:00+08:00 | cre | dedup-and-restart-loop-fixes
- action: add event path plus masked summary skeleton, adaptive dedup window, and cap automatic Weixin restarts
- command_or_ref: native/wechat-observer/hardware_observer.cpp; native/wechat-observer/wechat_monitor.cpp; ./tools/build-wechat-hook-harness.sh
- result_summary: restart loop eliminated (0 restarts in the new run), login reached LOGGED_IN and HOOK_ACTIVE on PID 23756, native counters now visible in local logs, uploads recovered to HTTP 200 after a transient Cloudflare egress outage
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [dedup_window=8s_with_time_marker_or_120s_without, auto_restart_cap_per_run=1, event_path_and_skeleton=logged_locally_only]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: one more real receipt to confirm a single event and a matched order

## 2026-09-28T21:28:00+08:00 | cre | dedup-verified-live
- action: verify one receipt produces exactly one event and matches a live order after the dedup and restart fixes
- command_or_ref: %LOCALAPPDATA%/CFPay/wechat-monitor.log; /api.php?act=order for wx-live-1790601896-1221; /admin-api/orders; /admin-api/monitors
- result_summary: masked skeleton is 微信支付收款#.#元(老顾客到店[朋友]) with no time marker so the 120s window applies; 0.02 and 0.01 receipts were captured 4-11ms after binding, uploaded HTTP 200, the 0.01 order became PAID with epay status=1, and no duplicate event appeared during the following 2.7 minutes
- artifacts: [../../docs/integration-journal.md, ../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [duplicate_suppression=verified, restart_loop=verified_fixed, capture_latency_ms=4_to_11, same_amount_within_120s=collapsed_by_design]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: keep the observer running for ordinary receipts and revisit the summary source only if a missed receipt is reported

## 2026-09-28T22:07:00+08:00 | cre | structured-field-discovery
- action: disable text-pattern payment emission and add a structured record slot dump diagnostic at the proven record append entry
- command_or_ref: native/wechat-observer/record_dump_observer.cpp; --run-weixin-record-diagnostic 23868; masked slot skeletons in local JSONL
- result_summary: diagnostic installed and healthy on PID 23868 with 77 threads covered; recordAppends stays 0 while the account is idle, confirming the append entry only fires on real service-session refresh
- artifacts: [../../native/wechat-observer/record_dump_observer.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [text_pattern_payment=disabled, discovery_target=record_append_0x490d5c0, coverage_interval_ms=250, fresh_weixin_required_per_diagnostic]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: capture one real receipt to dump record slots, identify structured amount/transaction/account/time offsets, then implement production capture by fixed offsets without text matching

## 2026-09-28T22:25:00+08:00 | cre | record-dump-output-fixed
- action: make the structured record dump write directly to a local file and fix the truncated dump path
- command_or_ref: native/wechat-observer/record_dump_observer.cpp; --run-weixin-record-diagnostic 18000; WeChatSlotDump/record-dump.jsonl
- result_summary: dump file now written and flushed per line with status every 5s, 111 threads covered, pipe kept only for handshake and failure codes; recordAppends stays 0 while idle
- artifacts: [../../native/wechat-observer/record_dump_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [dump_output=local_file_direct, pipe=handshake_only, path_bug=wide_format_fixed, append_entry=idle_silent]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: capture one real receipt to dump record slots and identify structured field offsets

## 2026-09-28T22:40:00+08:00 | cre | record-slot-discovery
- action: dump masked string slots of the 0x300 service-session record during a real receipt and add numeric field discovery
- command_or_ref: --run-weixin-record-diagnostic 18000; WeChatSlotDump/record-dump.jsonl
- result_summary: 11 appends and 58 slot records; session account id at +0x0a8, display name at +0x0c8, type label at +0x160, summary text at +0x048; no separate amount or transaction string slot found yet
- artifacts: [../../native/wechat-observer/record_dump_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [receipt_session_identifiable_by_structured_id=true, amount_field=unknown, numeric_dump=added]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: capture one receipt with a distinctive amount to locate the fen value and timestamp offsets

## 2026-09-28T23:03:00+08:00 | cre | structured-service-record-production
- action: move the production receipt hook to the structured record append entry and split responsibilities between the DLL and the monitor
- command_or_ref: native/wechat-observer/hardware_observer.cpp; native/wechat-observer/wechat_monitor.cpp; record-dump.jsonl seq 6-8
- result_summary: receipt assistant service id resolved as gh_f0a92aa7146c at +0x0a8/+0x000, type label 服务号 at +0x160, summary at +0x048; the record carries no amount, transaction id or timestamp field, so the monitor now triggers on the configured service id and parses the amount numerically
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [production_entry=+0x490d5c0, dll_role=capture_only, trigger=structured_service_id, amount=numeric_parse_only, jump_link=amount_independent, masked_xor_hypothesis=rejected]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: configure wechatReceiptServiceId, restore the production monitor and verify one receipt end to end with a different amount

## 2026-09-28T23:09:00+08:00 | cre | structured-capture-verified
- action: verify the structured service-record receipt path end to end with a 0.41 payment and redeploy cleanly
- command_or_ref: wechat-monitor.log; /api.php?act=order for wx-live-1790607886-1394; /admin-api/orders; /admin-api/monitors
- result_summary: service id gh_f0a92aa7146c matched structurally, amount 41 fen parsed numerically, the second record append of the same receipt was deduplicated, the order became PAID with epay status=1, and no new unmatched event appeared
- artifacts: [../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [structured_trigger=verified, text_matching=absent_from_production_dll, dedup_window_bucket=120s_verified, lastEventAt=only_on_enqueue]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: expose wechatReceiptServiceId in the native config window with discovered candidates, then keep the observer in ordinary service

## 2026-09-28T23:23:00+08:00 | cre | network-layer-structured-hunt
- action: remove all text amount parsing from the production path and add a NewSync command payload dump diagnostic
- command_or_ref: native/wechat-observer/sync_command_observer.cpp; --run-weixin-sync-diagnostic 23984; WeChatSyncDump/sync-dump.jsonl
- result_summary: baseline captured 8 responses and 11 commands with CmdIDs 1/5/23/44; a CmdID=5 heap payload holds a 282-byte XML-shaped string at +0x60, which is the first structured protocol-level candidate for receipt fields
- artifacts: [../../native/wechat-observer/sync_command_observer.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-001]
- decision_delta: [text_amount_parsing=removed, wechat_payment_reporting=paused_until_structured_source_found, next_probe=cmd5_payload_protocol_fields]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: capture one receipt with a distinctive amount and inspect the CmdID=5 payload protocol body for named amount/transaction/time fields

## 2026-09-28T23:55:00+08:00 | cre | paymsg-structured-production
- action: decode the CmdID=5 payload protobuf, recover the paymsg protocol fields, and switch production capture to the NewSync response parser
- command_or_ref: InspectWechatNewSyncCommandObjects.java; InspectWechatCmdItemPayload.java; native/wechat-observer/hardware_observer.cpp; src/monitor.ts
- result_summary: paymsg carries transid, fee in fen, feetype, timestamp, status, type=wx_f2f and outtradeno; production DR0 now hooks Weixin.dll+0x106fb10, event id is sha256(transid), and the full structured detail is uploaded with the payment event
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/wechat_host.cpp, ../../src/monitor.ts, ../../public/admin.js, ../../tools/ghidra/InspectWechatNewSyncCommandObjects.java, ../../tools/ghidra/InspectWechatCmdItemPayload.java, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [structured_amount=paymsg_fee_integer, transaction_id=transid, dedup=transid_based, display_text_usage=none, record_append_path=diagnostic_only]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: deploy the worker and monitor, then verify one receipt end to end with full detail stored

## 2026-09-29T00:10:00+08:00 | cre | panel-semantics-and-autovalidate
- action: remove the redundant hide-to-tray button and make target validation automatic with error-only reporting
- command_or_ref: native/wechat-observer/wechat_config.cpp; native/wechat-observer/wechat_monitor.cpp; native/wechat-observer/hardware_observer.cpp
- result_summary: startup now self-validates via WM_APP+20, the misleading "please re-validate" hint is gone, manual path edits re-validate on save, success popups were replaced by status-line text, and UTF-8 sanitizing removed the invalid-JSON host errors
- artifacts: [../../native/wechat-observer/wechat_config.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/hardware_observer.cpp, ../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [auto_validate=silent_unless_error, hide_button=removed, popup_policy=errors_only, utf8_truncation=fixed]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: verify one receipt end to end through the paymsg structured path with full detail uploaded

## 2026-09-29T00:16:00+08:00 | cre | paymsg-end-to-end-verified
- action: verify the structured paymsg receipt path end to end and fix the nested content offset
- command_or_ref: wechat-monitor.log; wrangler d1 payment_event; /api.php?act=order for wx-live-1790611845-8801
- result_summary: 0.53 receipt captured with fee=53 fen, transid 93 bytes, status=1, type=wx_f2f, paidAt taken from the protocol timestamp; order PAID, epay status=1, raw_excerpt 1366 chars stored as MATCHED; content extraction offset fixed and proven by a natural dynacfg sysmsg starting cleanly with XML
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../src/monitor.ts, ../../public/admin.js, ../../docs/integration-journal.md]
- evidence_ids: [E-STATIC-001, E-DYNAMIC-001]
- decision_delta: [production_receipt_source=paymsg_newsync, event_id=sha256_transid, amount=integer_fen_field, display_text_usage=none, content_offset_bug=fixed]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: keep the observer in ordinary service and delete the local discovery dumps

## 2026-09-29T00:46:00+08:00 | cre | admin-restructure-and-callback-verification
- action: split the admin settings into focused tabs, show devices only while heartbeating, merge login and component status per device, gate the QR behind an on-demand dialog, expose parsed rawdata per order/event, and verify merchant callbacks
- command_or_ref: public/admin.html; public/admin.js; public/styles.css; src/index.ts; src/monitor.ts; migrations/0008_callback_sink.sql; wrangler d1 migrations apply --remote; wrangler deploy
- result_summary: settings PUT is now partial-update, monitors API returns only devices with a heartbeat within 90s and stale devices are purged by the cron, QR renders only inside a dialog opened by a countdown button and closes on LOGGED_IN, order and unmatched rows open a detail dialog with structured fields plus the uploaded JSON and callback attempts, and callback verification showed SUCCESS on the happy path and RETRYING with HTTP_500 plus a 60s backoff on the failing path
- artifacts: [../../public/admin.html, ../../public/admin.js, ../../public/styles.css, ../../src/index.ts, ../../src/monitor.ts, ../../migrations/0008_callback_sink.sql, ../../native/wechat-observer/wechat_monitor.cpp, ../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [rawdata=parsed_json_only, devices=heartbeat_scoped, qr=on_demand_dialog, callback_sink=verification_facility]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: confirm the parsed-JSON payload of the next real receipt in the order detail dialog

## 2026-09-29T01:01:00+08:00 | cre | single-exe-distribution
- action: embed the observer DLL into the monitor EXE and release it at runtime
- command_or_ref: native/wechat-observer/embedded_observer.h; CMake add_dependencies + CFPAY_OBSERVER_DLL_FILE; tools/build-windows-wechat-monitor.mjs; SingleExeTest dry run
- result_summary: package is a single 5,282,878-byte EXE; an empty directory run extracted the DLL with sha256 a00b0668… and reached HOOK_ACTIVE, and the production directory recovered after its standalone DLL was deleted, with later runs reporting already present and identical
- artifacts: [../../native/wechat-observer/embedded_observer.h, ../../native/wechat-observer/wechat_monitor.cpp, ../../native/wechat-observer/CMakeLists.txt, ../../tools/build-windows-wechat-monitor.mjs, ../../tools/install-windows-wechat-monitor.ps1, ../../tools/verify-windows-wechat-single-exe.ps1, ../../docs/wechat-native-observer.md, ../../README.md, ../../docs/integration-journal.md]
- evidence_ids: []
- decision_delta: [distribution=single_exe, dll=embedded_and_extracted, in_place_upgrade=still_forbidden_while_weixin_running, memory_only_injection=not_adopted]
- carry_forward_refs: [scope.md, ../../docs/integration-journal.md]
- next: keep the single EXE as the production artifact and re-verify after any observer change

## 2026-11-01T03:15:00+08:00 | cre | weixin-4.2.0.11-static-reverse
- action: analyze the local Weixin 4.2.0.11 sample and recover NewSync, CmdList, CmdItem, account and nickname targets
- command_or_ref: Ghidra 12.1.4; InspectWechat42NewSyncChain.java; InspectWechat42KnownHooks.java; Weixin.dll SHA-256 `745cdf2955fa5128cdb00c056c97571156a5f107406a9bb15ac0754f3246b201`
- result_summary: confirmed NewSync parser RVA `0x10b0230`, response/list/item vtables `0x929f408/0x96d0558/0x96d04d8`, CmdID at item `+0x10`, account getter `0x0e07f70` called by `0x2915c3e`, and unchanged unique nickname signature at `0x4b690`; no runtime or real-payment test was performed
- artifacts: [../../tools/ghidra/InspectWechat42NewSyncChain.java, ../../tools/ghidra/InspectWechat42KnownHooks.java, evidence/E-static-weixin-4.2.0.11.md]
- evidence_ids: [E-STATIC-WEIXIN-4.2.0.11]
- decision_delta: [target_version=4.2.0.11, command_id_offset=0x10, network_write_diagnostic=not_migrated_without_evidence]
- carry_forward_refs: [scope.md, workitems.md, ../../docs/wechat-native-observer.md]
- next: build and statically validate the adapted single EXE; wait for owner-operated live/payment verification

## 2026-11-01T03:23:00+08:00 | lead | weixin-4.2.0.11-native-adaptation
- action: adapt the production observer and single-EXE package to the statically confirmed 4.2 structures
- command_or_ref: tools/build-single-exe.sh; Weixin.dll sample SHA-256; CMake OBJECT_DEPENDS; focused and full Vitest suites
- result_summary: production NewSync hook now validates response/list/item vtables and reads CmdItem's string object at +0x08; corrected the capture call to pass the CmdItem itself; EXE self-test and embedded-DLL validation passed after fixing the missing incremental-build dependency; 131 tests and both TypeScript checks passed
- artifacts: [../../native/wechat-observer/wechat_target.h, ../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/CMakeLists.txt, ../../docs/wechat-native-observer.md, ../../docs/monitor-protocol.md, evidence/E-static-weixin-4.2.0.11.md]
- evidence_ids: [E-STATIC-WEIXIN-4.2.0.11]
- decision_delta: [production_newsync=adapted, cmditem_string_pointer=+0x08, embedded_exe_incremental_dependency=fixed, runtime_hook=not_tested, live_payment=requires_owner]
- carry_forward_refs: [scope.md, workitems.md, ../../docs/integration-journal.md]
- next: run Windows host inspection when Windows PowerShell interop is available; then validate naturally received NewSync and owner-operated payment

## 2026-11-01T03:54:00+08:00 | lead | weixin-4.2.0.11-runtime-validation
- action: fix the remaining 4.1 account-callsite signature, install the rebuilt monitor, and validate the observer in the user's interactive Windows session
- command_or_ref: tools/build-single-exe.sh; native monitor logs; Windows process-memory signature checks
- result_summary: corrected observer callsite bytes from `e8 5c bb 4a fe` to `e8 2d 23 4f fe`; 4.2 runtime reached `HEALTHY / HOOK_ACTIVE`, account and nickname hooks fired, NewSync and paymsg scan counters advanced; no payment event observed
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../tests/wechat-native-log-language.test.mjs, evidence/E-dynamic-weixin-4.2.0.11.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-WEIXIN-4.2.0.11]
- decision_delta: [runtime_hook=validated, account_callsite_signature=4.2, real_payment=waiting_for_owner]
- carry_forward_refs: [evidence/E-static-weixin-4.2.0.11.md, evidence/E-dynamic-weixin-4.2.0.11.md, workitems.md]
- next: observe one owner-operated real payment and verify parsed fields reach the Worker order flow

## 2026-11-01T04:10:00+08:00 | lead | weixin-4.2.0.11-metric-separation
- action: separate NewSync, AddMsg and paymsg observer counters and prepare a clean Windows installation
- command_or_ref: native observer counters; Windows process tree and loaded-module inspection; single-EXE build
- result_summary: old receiptEntries mixed NewSync entries with AddMsg commands; updated counters passed focused tests and native self-test, then all five old Weixin processes were stopped before reinstall; new DLL reached HEALTHY/HOOK_ACTIVE with NewSync 4/4, AddMsg 4, paymsg scans 2, payment messages 0, read failures 3 and queue drops 0
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../native/wechat-observer/wechat_monitor.cpp, ../../tests/wechat-native-log-language.test.mjs, ../../docs/wechat-native-observer.md, ../../docs/integration-journal.md, evidence/E-dynamic-weixin-4.2.0.11.md]
- evidence_ids: [E-DYNAMIC-WEIXIN-4.2.0.11]
- decision_delta: [metrics=split_by_stage, old_dll=must_unload_before_replacement, real_payment=owner_required]
- carry_forward_refs: [evidence/E-static-weixin-4.2.0.11.md, evidence/E-dynamic-weixin-4.2.0.11.md, workitems.md]
- next: observe an owner-operated real payment and verify parsed fields reach the Worker order flow

## 2026-11-01T04:10:30+08:00 | lead | weixin-4.2.0.11-idle-sync-observation
- action: continue observing the installed 4.2 hook without initiating a payment
- command_or_ref: Windows monitor log; Worker `/api/monitor/status`; process/module hash audit
- result_summary: hook remained HEALTHY/HOOK_ACTIVE; counters advanced to NewSync 10/10, AddMsg 8 and paymsg scans 5, while payment messages and queue drops remained zero; aggregate read failures reached 6 and cannot be attributed without a payment sample
- artifacts: [evidence/E-dynamic-weixin-4.2.0.11.md, ../../docs/integration-journal.md]
- evidence_ids: [E-DYNAMIC-WEIXIN-4.2.0.11]
- decision_delta: [runtime_newsynchronization=confirmed, real_payment_parser_validation=still_required, read_failure_source=unknown]
- carry_forward_refs: [evidence/E-static-weixin-4.2.0.11.md, evidence/E-dynamic-weixin-4.2.0.11.md, workitems.md]
- next: administrator performs a real small payment, then verify structured event fields and Worker order matching

## 2026-11-01T04:35:00+08:00 | lead | weixin-4.2.0.11-nested-paymsg-layout-fix
- action: trace repeated paymsg metadata failures to the nested protobuf object layout, fix the pointer chain, rebuild, and install the single EXE
- command_or_ref: Ghidra 12.1.4 functions `FUN_182fcbe10`, `FUN_1806a2ef0`, `FUN_181641e70`; `bash tools/build-single-exe.sh --self-test`; Windows monitor log
- result_summary: CmdItem `+0x08` points to AddMsg, not std::string; AddMsg field 2 uses present bit `+0x18 bit 1` and its string object at `+0x08`. Production capture now checks that bit and follows both pointers before reading string metadata. Full tests (134 passed, 2 skipped), typecheck, Windows self-test, embedded-DLL check and installed-binary hash comparison passed. New monitor reports successfully; Weixin is currently `LOGIN_REQUIRED`, so post-fix payment parsing is not yet dynamically verified.
- artifacts: [../../native/wechat-observer/hardware_observer.cpp, ../../tests/wechat-native-log-language.test.mjs, evidence/E-static-weixin-4.2.0.11.md, evidence/E-dynamic-weixin-4.2.0.11.md]
- evidence_ids: [E-STATIC-WEIXIN-4.2.0.11, E-DYNAMIC-WEIXIN-4.2.0.11]
- decision_delta: [cmditem_payload=AddMsg_pointer, addmsg_content=present_bit_1_plus_nested_string, metadata_failure_root_cause=wrong_pointer_level, live_payment=not_performed]
- carry_forward_refs: [scope.md, workitems.md, evidence/E-static-weixin-4.2.0.11.md, evidence/E-dynamic-weixin-4.2.0.11.md]
- next: owner completes WeChat login; confirm post-fix NewSync/paymsg counters, then owner performs a real small payment to validate event parsing and Worker order matching
