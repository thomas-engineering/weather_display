# ESP-Hosted OTA Debugging: RPC v1 vs v2 Protocol Mismatch

Summary of a debugging session on the `esp32-p4-c6-espnow-enabler` project
(P4 host + C6 coprocessor, `espressif/esp-hosted-mcu`). Kept here because the
same host↔coprocessor OTA pattern (esp-hosted, SDIO, ESP32-P4 + ESP32-C6)
applies to this project if/when a WiFi coprocessor is added.

## Symptom

`idf.py flash monitor` on the P4 host produced, on every attempt:

```
E (nnnn) ota_littlefs: Failed to end OTA: ESP_ERR_OTA_VALIDATE_FAILED
```

All chunk writes during the LittleFS→SDIO OTA transfer reported `ESP_OK`,
but the coprocessor's final `esp_ota_end()` rejected the reconstructed image
as corrupted.

## Ruled out (in order tested)

| Hypothesis | Test | Result |
|---|---|---|
| OTA chunk size too large / frame fragmentation | Reduced `CHUNK_SIZE` 1400 → 512 bytes | Same failure, unrelated |
| SDIO signal integrity / clock too high | Reduced `ESP_HOSTED_HOST_SDIO_CLK_KHZ` 40000 → 10000 | Same failure, unrelated |
| Wrong/corrupt firmware binary being transferred | Rebuilt the correct `examples/ota/coprocessor_ota/cp` project from source, matching host commit exactly | Same failure, unrelated |

## Root cause: RPC protocol version mismatch

The host's serial log contained the actual clue, easy to miss:

```
I (2322) eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=2.6.7
E (2328) eh_init_evt: major version mismatch — OTA coprocessor from host
```

The factory-installed coprocessor firmware (v2.6.7) speaks the **RPC v1**
wire protocol. The current host library (v3.0.7) only has an **RPC v2**
client. When negotiation fails to confirm a version, the host logs
`"Default RPC to V2"` and just assumes v2 — it has no fallback, because:

- Host side: only `host/features/eh_host_feat_rpc_ext_v2` exists. There is
  no `eh_host_feat_rpc_ext_v1` at all.
- Coprocessor side: legacy v1 handlers still exist
  (`coprocessor/features/eh_cp_feat_rpc_ext_v1/*`, including a separate
  `req_ota_write_handler` with a different message struct than the v2
  handler in `eh_cp_feat_rpc_ext_v2_handler_req_system.c`).
- A shared v1 serializer exists (`common/serializers/eh_proto_v1/`,
  generated from `rpc_v1.proto`) but is currently only compiled as a stub
  (`eh_proto_v1_stub.c`) — not wired up to a real host client.

Because basic RPC dispatch (matching by numeric RPC ID) happens to work
across the two formats for small control messages, the mismatch only
becomes visible once a large multi-chunk transfer (the OTA image) is
reconstructed byte-for-byte on the v1-speaking coprocessor from v2-encoded
frames — the corruption is silent until the final image-validation step.

## Fix applied (one-time bootstrap, not a permanent code change)

1. Built the correct coprocessor project for the target chip directly from
   source, matching the host's commit:
   ```
   cd esp-hosted-mcu/examples/ota/coprocessor_ota/cp
   idf.py set-target esp32c6
   idf.py build
   ```
2. Flashed that binary **directly to the coprocessor's own UART** (separate
   USB-serial adapter wired to the coprocessor's TX/RX/EN/boot-strap pins),
   bypassing the P4 host and the broken SDIO OTA path entirely:
   ```
   esptool --chip esp32c6 -p <PORT> write-flash \
       0x0     bootloader.bin \
       0x8000  partition-table.bin \
       0xd000  ota_data_initial.bin \
       0x10000 eh_cp_ota_coprocessor_ota.bin
   ```
3. After reboot, host and coprocessor negotiated matching versions:
   ```
   I (2110) eh_init_evt: esp-hosted fw versions: host=3.0.7 coprocessor=3.0.7 (match)
   ```
4. A subsequent **normal SDIO OTA from the P4 host** (the actual thing being
   tested) then completed cleanly end-to-end:
   ```
   I (7603) ota_littlefs: LittleFS OTA completed successfully
   I (7634) host_performs_slave_ota: OTA completed successfully!
   ```

This confirms the SDIO OTA transport and `ota_littlefs` flow are correct;
the only real bug was the protocol-version gap on first contact with
factory-old coprocessor firmware.

## Could RPC v1 support be added to the host so *any* old coprocessor can be OTA'd directly?

Assessed but not implemented — feasible, not a small patch:

- Would need a new `eh_host_feat_rpc_ext_v1` client module on the host,
  mirroring the existing v2 client's structure against the already-generated
  `gen_v1.h`/`gen_v1.c` serializer.
- Would need real version detection (instead of the current
  "assume v2" default) and per-connection dispatch to the right encoder.
- Coprocessor-side v1 handlers already exist, so only the host side is
  missing — but that's still a genuine new feature, not a config flag.
- For a one-time factory→current migration, bootstrapping once via direct
  UART flash (as done here) is the pragmatic choice; maintaining two live
  RPC protocol stacks in the host is only worth it if OTA-updating
  in-the-field v1 coprocessors *without* physical UART access is a hard
  requirement.

## Relevance to this project

If a WiFi/BT coprocessor (ESP32-C6 or similar via esp-hosted) is ever added
here, budget for the same first-contact issue if the coprocessor ships with
older factory firmware: check the `eh_init_evt` log line for a
`major version mismatch` warning before assuming OTA will work, and keep a
UART bootstrap path available as a fallback.
