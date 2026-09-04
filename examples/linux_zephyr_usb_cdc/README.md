# Linux to Zephyr over USB CDC ACM

The typed Linux gateway publishes bounded commands to a typed `dev_c` Zephyr
sink. Both endpoints compile against the same `Command` Interface generated
from `command.proto`. The deployment maps the route to `usb0` and binds that
transport to the board's `usb_cdc` Devicetree resource and the Linux host's
`/dev/ttyACM0` TTY resource. Inventory records the expected device identity,
while `deploy plan` remains non-mutating. This example uses
`dev_c/stm32f407xx` as the first USB deployment acceptance target.

```sh
uv run aster resolve examples/linux_zephyr_usb_cdc/workspace.yaml \
  examples/linux_zephyr_usb_cdc/deployment.yaml --release
uv run aster codegen examples/linux_zephyr_usb_cdc/workspace.yaml \
  examples/linux_zephyr_usb_cdc/deployment.yaml build/generated/usb-cdc \
  --release
uv run aster deploy plan examples/linux_zephyr_usb_cdc/deployment.yaml examples/linux_zephyr_usb_cdc/inventory.yaml build/generated/usb-cdc
```

Deployment code generation emits the bounded header and both Node compositions
in one deterministic tree. The generated Zephyr endpoint registers its Channel
route, starts the CDC ACM framing Adapter, and polls it from the resolved
executor without changing ``CommandSink``. The Linux endpoint and physical
end-to-end link remain release work; physical enumeration is still an explicitly
pending hardware verification item for v0.2.0.
