# Deployment and artifact installation

## Separate from local execution

Ordinary Linux runs use runtime.yaml and aster run, without a Bundle or
Deployment Lock. Optional v1alpha3 deployment.yaml will handle cross-node/Zephyr
placement and communication constraints. That compiler is not complete, so this
page does not present speculative configuration as an executable workflow.

## Existing installation tools: legacy inputs

deploy plan/apply/status still read v1alpha2 Deployment, Inventory and Bundle
inputs. Bundles record file hashes, sizes and the legacy Deployment ID; plan
verifies them before describing actions. The generator currently bundles
generated inputs; build-artifact descriptions are not yet independent deployment
inputs.

`deploy apply --execute` stages and verifies files using Local/SSH Adapters,
switches current, and retains previous plus .aster-deploy-state.yaml for
inspection and rollback. It **does not call systemctl, start/restart applications,
or flash MCUs**. Serial/debug-probe targets are rejected. Switching directories
does not establish that a robot is running or healthy.

Inventory stores no credentials. SSH uses the operator's agent/config and
requires explicit --execute for remote operations. SSH transfers artifacts;
it is not the application data Transport.

These legacy regression commands do not accept the new runtime.yaml:

```sh
aster codegen workspace.yaml deployment.yaml build/generated
aster deploy plan deployment.yaml inventory.yaml build/generated
aster deploy apply deployment.yaml inventory.yaml build/generated --execute
aster deploy status inventory.yaml
```

## Remaining integration

The new compiler must preserve business configuration across placements,
generate node inputs and check actual registrations. Communication compatibility
must depend on communication contracts, not logging/business parameters; artifact
integrity hashes remain separate. Existing handshakes still use the legacy full
Deployment ID, so this separation is not implemented.

Built-artifact descriptions, systemd configuration, installation and rollback
need end-to-end verification. Directory activation and process lifecycle are
different operations; documentation must not imply the latter already exists.
