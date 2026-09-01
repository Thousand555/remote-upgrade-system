# Firmware release storage

Each release is an immutable directory named after its `firmware_id`:

```text
server/firmware/
└── f407-node-1.2.0/
    ├── firmware.bin    # M7-compatible stm_fw cache package
    └── manifest.json   # M8 HTTP manifest
```

Create a release through `tools/pack_firmware.py`; do not hand-edit the
manifest or replace `firmware.bin` in an existing release directory. Firmware
binaries are intentionally ignored by Git. Preserve approved releases in the
project artifact store together with their Manifest and SHA-256 values.
