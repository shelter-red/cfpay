# Work Items

| ID | title | role | targets | surface | status | evidence | notes |
|----|-------|------|---------|---------|--------|----------|-------|
| WI-001 | Establish scope and auth | lead | case | process | completed | E-TRIAGE-001 | Scope, roles and exclusions reviewed |
| WI-002 | Triage fixed Weixin.dll sample | cre | Weixin.dll | binary | completed | E-TRIAGE-001 | Hash, type, imports, exports and packer indicators recorded |
| WI-003 | Locate global session preview binding | cre | Weixin.dll | binary | completed | E-STATIC-001 | ChatSessionCellViewModel binding and digest field identified |
| WI-004 | Validate session digest path dynamically | cre | Weixin.exe | process_memory | in_progress | E-DYNAMIC-001 | Fresh receipt validates the binding; expanded model and rich-text shape diagnostic is ready for restored-session validation |
| WI-005 | Reverse Weixin 4.2.0.11 NewSync structures | cre | Weixin.dll | binary | completed | E-STATIC-WEIXIN-4.2.0.11 | Confirmed response, command-list and CmdItem parsers, vtables, fields, account getter and nickname signature |
| WI-006 | Adapt native observer to 4.2.0.11 | lead | C++ monitor | source | completed | E-STATIC-WEIXIN-4.2.0.11, E-DYNAMIC-WEIXIN-4.2.0.11 | Corrected nested CmdItem -> AddMsg -> std::string pointer chain; build, EXE self-test, interactive-session login and HEALTHY runtime validation pass |
| WI-007 | Validate live NewSync and payment path | lead | Weixin.exe | process_memory | in_progress | E-DYNAMIC-WEIXIN-4.2.0.11 | After the new build was installed, Weixin requires login; no post-fix payment sample exists, so structured receipt capture and order matching remain unverified |

## Coverage
- [x] Triage complete for in_scope sample
- [ ] Critical/High candidates triaged (or N/A for pure RE)
- [x] Validated findings have Evidence (E-*)
- [ ] Path documented (attack/call/solve)
- [ ] Timeline continuous across major phases
- [ ] Report via docs-generator
- [ ] field-journal anonymized

## Refs
- skills/ops/timeline-workitem.md
- skills/ops/evidence-finding-path.md
