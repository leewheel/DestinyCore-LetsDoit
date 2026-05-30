# opcode_coverage

Read-only diagnostic that turns a worldserver log into an opcode-coverage
report. It does **not** touch the core or the client — it only reads what the
server already logs on the `network.opcode` / `network` channels.

## What it reports

From the messages the core emits for traffic it does not fully handle:

- `Received unhandled opcode ...`   — opcode wired to `Handle_NULL`
- `Received not handled opcode ...` — opcode disabled via `STATUS_UNHANDLED`
- `requested unsupported unknown hotfix type: N` — DB2/hotfix gap

it produces:

- unhandled opcodes **deduplicated and sorted by frequency** (what recurs most
  is what to look at first), with the distinct account count;
- a **triage** into `needs review` / `query` / `ack` / `telemetry` so obvious
  fire-and-forget client chatter (e.g. `CMSG_REPORT_*`) is not mistaken for a
  missing feature;
- each opcode annotated with its **registered** status and handler (read from
  `Opcodes.cpp`), so a true `Handle_NULL` is told apart from a disabled handler;
- a separate section for hotfix/DB2 gaps.

## Usage

```sh
# enable the logger first (worldserver.conf):
#   Logger.network.opcode=4,Console Server
# then play through the features you care about and run:

contrib/opcode_coverage/opcode_coverage.py path/to/worldserver.log
cat worldserver.log | contrib/opcode_coverage/opcode_coverage.py
contrib/opcode_coverage/opcode_coverage.py worldserver.log --opcodes /path/to/Opcodes.cpp
```

`Opcodes.cpp` is auto-detected relative to the script; pass `--opcodes` if your
log was captured elsewhere.
