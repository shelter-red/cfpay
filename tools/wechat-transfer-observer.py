#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
import threading
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


TAG_FIELDS = {
    "paysubtype": "pay_subtype",
    "feedesc": "fee_description",
    "transcationid": "transaction_id",
    "transactionid": "transaction_id",
    "begintransfertime": "begin_transfer_time",
    "pay_memo": "memo",
    "receiver_username": "receiver_username",
}


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_transfer_content(content: str) -> dict[str, str]:
    if not content.strip():
        return {}

    roots: list[ET.Element] = []
    for candidate in (content, f"<observer_root>{content}</observer_root>"):
        try:
            roots.append(ET.fromstring(candidate))
            break
        except ET.ParseError:
            continue

    if not roots:
        return {"content_parse_error": "invalid_xml"}

    parsed: dict[str, str] = {}
    for element in roots[0].iter():
        output_name = TAG_FIELDS.get(local_name(element.tag).lower())
        value = (element.text or "").strip()
        if output_name and value and output_name not in parsed:
            parsed[output_name] = value
    return parsed


def emit_json(value: dict[str, Any], stream: Any = sys.stdout) -> None:
    print(json.dumps(value, ensure_ascii=False, separators=(",", ":")), file=stream, flush=True)


def handle_message(message: dict[str, Any], data: bytes | None) -> None:
    if message.get("type") != "send":
        emit_json({"type": "frida_error", "message": message}, sys.stderr)
        return

    payload = message.get("payload")
    if not isinstance(payload, dict):
        emit_json({"type": "observer_protocol_error", "payload": payload}, sys.stderr)
        return

    if payload.get("type") == "observer_ready":
        emit_json(payload, sys.stderr)
        return

    if payload.get("type") == "wechat_transfer":
        fields = payload.get("fields")
        if isinstance(fields, dict):
            content = fields.get("content")
            if isinstance(content, str):
                payload["parsed_content"] = parse_transfer_content(content)

    emit_json(payload)


def run_self_test() -> None:
    sample = (
        "<msg><appmsg><wcpayinfo>"
        "<paysubtype>1</paysubtype>"
        "<feedesc><![CDATA[￥0.01]]></feedesc>"
        "<transcationid>test-transaction</transcationid>"
        "<begintransfertime>1727424000</begintransfertime>"
        "<pay_memo><![CDATA[test memo]]></pay_memo>"
        "<receiver_username>wxid_receiver</receiver_username>"
        "</wcpayinfo></appmsg></msg>"
    )
    parsed = parse_transfer_content(sample)
    expected = {
        "pay_subtype": "1",
        "fee_description": "￥0.01",
        "transaction_id": "test-transaction",
        "begin_transfer_time": "1727424000",
        "memo": "test memo",
        "receiver_username": "wxid_receiver",
    }
    if parsed != expected:
        raise RuntimeError(f"self-test failed: {parsed!r}")
    emit_json({"type": "self_test_ok", "parsed_content": parsed})


def observe(pid: int, duration: float, script_path: Path) -> None:
    try:
        import frida
    except ImportError as error:
        raise RuntimeError("frida is not installed in this Python environment") from error

    source = script_path.read_text(encoding="utf-8")
    device = frida.get_local_device()
    session = device.attach(pid)
    script = session.create_script(source)
    script.on("message", handle_message)

    try:
        script.load()
        print(
            f"observer attached to PID {pid}; duration={duration:g}s",
            file=sys.stderr,
            flush=True,
        )
        if duration > 0:
            threading.Event().wait(duration)
        else:
            threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            script.unload()
        finally:
            session.detach()
        print("observer detached", file=sys.stderr, flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Observe decoded Weixin transfer display fields")
    parser.add_argument("--pid", type=int, help="Weixin process ID containing Weixin.dll")
    parser.add_argument("--duration", type=float, default=30, help="seconds before automatic detach; 0 waits until interrupted")
    parser.add_argument(
        "--script",
        type=Path,
        default=Path(__file__).with_name("wechat-transfer-observer.js"),
        help="Frida JavaScript path",
    )
    parser.add_argument("--self-test", action="store_true", help="validate content parsing without attaching")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return
    if not args.pid or args.pid <= 0:
        parser.error("--pid is required unless --self-test is used")
    if args.duration < 0:
        parser.error("--duration must be zero or greater")
    if not args.script.is_file():
        parser.error(f"Frida script was not found: {args.script}")

    observe(args.pid, args.duration, args.script)


if __name__ == "__main__":
    main()
