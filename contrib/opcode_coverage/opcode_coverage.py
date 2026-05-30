#!/usr/bin/env python3
# Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
#
# Read-only diagnostic: turns a worldserver log into an opcode-coverage report.
#
# It scans the log for the three messages the core emits when it receives
# something it does not fully handle, then aggregates and triages them:
#
#   * "Received unhandled opcode ..."   -> opcode wired to Handle_NULL
#   * "Received not handled opcode ..." -> opcode disabled via STATUS_UNHANDLED
#   * "requested unsupported unknown hotfix type: N" -> DB2/hotfix gap
#
# When Opcodes.cpp is found, each unhandled opcode is annotated with the status
# and handler it is actually registered with, so a real Handle_NULL is told
# apart from a registered-but-disabled handler.
#
# Usage:
#   opcode_coverage.py worldserver.log
#   cat worldserver.log | opcode_coverage.py
#   opcode_coverage.py worldserver.log --opcodes /path/to/Opcodes.cpp

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

# "Received [not handled|unhandled] opcode [NAME 0xHEX (DEC)] from [... Account: N]"
OPCODE_RE = re.compile(
    r"Received (?:not handled|unhandled) opcode "
    r"\[(?P<name>.+?) 0x(?P<hex>[0-9A-Fa-f]+) \((?P<dec>\d+)\)\]"
    r"(?:.*?Account: (?P<account>\d+))?"
)

HOTFIX_RE = re.compile(r"requested unsupported unknown hotfix type:\s*(?P<type>\d+)")

# DEFINE_HANDLER(CMSG_FOO, STATUS_X, PROCESS_Y, &WorldSession::HandleFoo)
HANDLER_RE = re.compile(
    r"DEFINE_HANDLER\(\s*(?P<name>\w+)\s*,\s*(?P<status>\w+)\s*,"
    r"\s*\w+\s*,\s*&WorldSession::(?P<handler>\w+)\s*\)"
)

# Substring heuristics for triage. Deliberately conservative: only opcodes that
# are well-known fire-and-forget client telemetry are flagged as safe to leave
# as a no-op. Everything else is left for human review.
TELEMETRY_HINTS = (
    "REPORT_", "ENABLED_ADDONS", "CLIENT_VARIABLES", "KEYBINDING",
    "CLOSE_INTERACTION", "LOG_DISCONNECT", "_PERF", "ADDON_LIST",
)


def load_registered_opcodes(path):
    """Map opcode name -> (status, handler) from Opcodes.cpp, if available."""
    registered = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = HANDLER_RE.search(line)
                if match:
                    registered[match.group("name")] = (
                        match.group("status"), match.group("handler"))
    except OSError:
        return None
    return registered


def classify(name):
    if any(hint in name for hint in TELEMETRY_HINTS):
        return "telemetry (safe to leave no-op)"
    if name.endswith("_ACK"):
        return "ack (usually safe)"
    if "QUERY" in name or "REQUEST" in name:
        return "query (client may expect a reply)"
    return "needs review"


def find_default_opcodes_cpp():
    here = os.path.dirname(os.path.abspath(__file__))
    candidate = os.path.join(
        here, "..", "..", "src", "server", "game",
        "Server", "Protocol", "Opcodes.cpp")
    candidate = os.path.normpath(candidate)
    return candidate if os.path.isfile(candidate) else None


def parse_log(stream):
    counts = Counter()
    accounts = defaultdict(set)
    hexes = {}
    hotfix_types = Counter()

    for line in stream:
        match = OPCODE_RE.search(line)
        if match:
            name = match.group("name")
            counts[name] += 1
            hexes[name] = match.group("hex").upper()
            if match.group("account"):
                accounts[name].add(match.group("account"))
            continue

        hotfix = HOTFIX_RE.search(line)
        if hotfix:
            hotfix_types[hotfix.group("type")] += 1

    return counts, accounts, hexes, hotfix_types


def main():
    parser = argparse.ArgumentParser(description="Worldserver opcode-coverage report.")
    parser.add_argument("log", nargs="?", help="worldserver log file (default: stdin)")
    parser.add_argument("--opcodes", help="path to Opcodes.cpp (default: auto-detect)")
    args = parser.parse_args()

    if args.log:
        with open(args.log, encoding="utf-8", errors="replace") as handle:
            counts, accounts, hexes, hotfix_types = parse_log(handle)
    else:
        counts, accounts, hexes, hotfix_types = parse_log(sys.stdin)

    registered = load_registered_opcodes(args.opcodes or find_default_opcodes_cpp() or "")

    if not counts and not hotfix_types:
        print("No unhandled opcodes or hotfix gaps found in the log.")
        return

    # Group opcodes by triage bucket, ordered by total frequency.
    buckets = defaultdict(list)
    for name, count in counts.items():
        buckets[classify(name)].append(name)

    bucket_order = [
        "needs review",
        "query (client may expect a reply)",
        "ack (usually safe)",
        "telemetry (safe to leave no-op)",
    ]

    total_hits = sum(counts.values())
    print(f"Unhandled opcodes: {len(counts)} distinct, {total_hits} hits total\n")

    for bucket in bucket_order:
        names = buckets.get(bucket)
        if not names:
            continue
        print(f"== {bucket} ==")
        for name in sorted(names, key=lambda n: counts[n], reverse=True):
            line = f"  {counts[name]:6d}x  0x{hexes[name]:<5} {name}"
            seen = len(accounts.get(name, ()))
            if seen:
                line += f"  ({seen} account{'s' if seen != 1 else ''})"
            if registered is not None:
                status, handler = registered.get(name, ("UNREGISTERED", "-"))
                line += f"  [{status} -> {handler}]"
            print(line)
        print()

    if hotfix_types:
        print("== hotfix/DB2 gaps (CMSG_DB_QUERY_BULK) ==")
        for table_hash, count in hotfix_types.most_common():
            print(f"  {count:6d}x  unknown hotfix table hash {table_hash}")
        print()

    if registered is None:
        print("note: Opcodes.cpp not found; pass --opcodes to annotate handler status.")


if __name__ == "__main__":
    main()
