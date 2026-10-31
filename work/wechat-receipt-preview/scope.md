# Case Scope

## meta
- case_id: wechat-receipt-preview
- created: 2026-09-28T14:41:47+08:00
- operator: local
- project_root: /path/to/cfpay
- primary_skill: ghidra-reverse/SKILL.md
- primary_id: R22
- lead_role: lead
- specialist_roles: [cre]
- hint: 使用Ghidra逆向本机Weixin.dll 4.2.0.11，重新适配NewSync解包后结构化收款Hook
- preset: offline-sample

## auth
- status: granted
- basis: own_system
- evidence_of_auth: preset:offline-sample (owner-operated local file)
- MUST NOT proceed if status != granted

## in_scope
- assets:
  - /mnt/d/Program Files/Tencent/Weixin/4.2.0.11/Weixin.dll
- surfaces: [binary, process_memory, ui_binding]
- activities: [reverse, hook_validate, report]

## out_of_scope
- assets: []
- activities: [dos, phishing_real_users, unrestricted_exfil, network_hook, notification_read, ocr, database_decrypt]

## network_profile
- mode: offline
- notes: |
    offline | lab_only | authorized_target_only | unrestricted_lab
    Change mode only after auth.status = granted.
    Presets: offline-sample | ctf-public | own-system

## deliverables
- report: true
- field_journal: true
- diagrams: true
- timeline: true

## constraints
- timebox: {}
- stealth: low
- data_handling: anonymize

## signoff
- ready_for_act: true
- checklist:
  - [x] auth.status = granted
  - [x] in_scope.assets non-empty OR offline sample path set
  - [x] network_profile.mode chosen
  - [x] out_of_scope reviewed
  - [x] roles assigned (see skills/ops/role-map.md)

## ops_refs
- skills/ops/scope-contract.md
- skills/ops/evidence-finding-path.md
- skills/ops/role-map.md
- skills/ops/timeline-workitem.md
- skills/ops/IDENTITY.md
