# AimRT documentation structure and AsterCtrl gap analysis

> This audit records the state before the bilingual Sphinx and integrated API
> work. Its Doxygen and Chinese-documentation gaps were closed in the following
> implementation; the remaining items are still useful as roadmap context.
> Its Application/Deployment and Provider recommendations also predate the
> configuration-driven convergence decision. Use the [current audit](convergence-audit.md)
> for implementation status and priorities, not this historical comparison.

Date: 2026-09-05

## Conclusion

AimRT's documentation is not just a collection of tutorials. It has four distinct
layers:

1. a bilingual Sphinx site for learning and operational guidance;
2. separate, task-oriented manuals for every public C++ and Python service;
3. component-by-component runtime configuration references;
4. release history and generated Doxygen API pages.

AsterCtrl already has a clearer architecture story for its own domain: the
Application/Deployment split, bounded Protobuf, Linux/Zephyr portability,
resource budgets and deterministic deployment are all represented. The main
gap is the path from those concepts to routine engineering work. A reader can
understand *why* AsterCtrl is designed this way, but cannot yet look up every
public Interface, every YAML field, every generated artifact, or every supported
example from the documentation site.

The highest-value next work is therefore not to copy AimRT's directory tree
literally. It is to add:

- a public C/C++ Interface manual;
- a complete Graph and Package schema reference;
- an end-to-end examples catalog and board bring-up tutorial;
- per-version release notes.

Doxygen, multilingual output, version switching and performance reports should
follow after the authored reference content is complete enough to support them.

## Scope and evidence

This review uses only the two local repositories as first-party evidence:

- `<aimrt>` is the local AimRT checkout at commit `19ae48e`;
- `<asterctrl>` is the AsterCtrl checkout at commit `cf71438`, including its
  current working-tree documentation changes.

Generated AsterCtrl files under `build/document/` are excluded from source-page
counts. The comparison is structural and task-oriented; raw line counts are
only a rough indication of maturity, not a quality score.

At this snapshot, each AimRT language tree contains 101 Markdown source pages
and the AsterCtrl tree contains 23 Markdown source pages. AimRT's Chinese tree
has about 13,739 Markdown lines, while AsterCtrl has about 964. More important
than the difference in volume is the difference in coverage described below.

Sources: `<aimrt>/document/sphinx-cn`, `<aimrt>/document/sphinx-en`,
`<asterctrl>/document/sphinx-en`.

## What AimRT's `document` contains

### 1. Generated API documentation

`document/doxygen/` is an independent Doxygen pipeline:

- `Doxyfile` scans `src/common`, `src/interface` and `src/runtime/core`
  recursively;
- it extracts C/C++ declarations and comments and generates searchable HTML;
- `build.sh` invokes Doxygen;
- `dockerfile/` packages generated HTML into an Nginx image.

The root CMake build invokes this pipeline when `AIMRT_BUILD_DOCUMENT` is
enabled on Unix. The Doxygen site is separate from the authored Sphinx site;
the Sphinx configuration does not use Breathe or Doxygen XML.

Sources: `<aimrt>/document/doxygen/Doxyfile`,
`<aimrt>/document/doxygen/build.sh`,
`<aimrt>/document/doxygen/dockerfile/Dockerfile`,
`<aimrt>/CMakeLists.txt`.

### 2. Two authored language trees

AimRT maintains two nearly parallel Sphinx roots:

- `document/sphinx-cn/`;
- `document/sphinx-en/`.

Each tree has its own `conf.py`, `index.md`, build script, static CSS, version
selector template, contact page, release notes and tutorial hierarchy. The
sources are MyST Markdown even though Sphinx also accepts reStructuredText.

The Sphinx configuration enables MyST, `sphinx_design`, autodoc helpers and
`sphinx_multiversion`. The build script can build one version for local editing
or build a whitelist of Git tags, then creates a `latest` symlink. A second
Docker/Nginx directory packages the authored HTML site.

Sources: `<aimrt>/document/sphinx-cn/conf.py`,
`<aimrt>/document/sphinx-cn/build.sh`,
`<aimrt>/document/sphinx-cn/_templates/versions.html`,
`<aimrt>/document/sphinx-cn/dockerfile/`, and the corresponding files under
`sphinx-en/`.

### 3. Quick-start material

The quick-start section contains:

- development-container setup;
- C++ installation and consumption;
- Python installation and consumption;
- C++ Hello World;
- Python Hello World;
- Ubuntu source-build instructions;
- Windows source-build instructions.

The two source-build files exist in the tree, although they are not included in
the current `tutorials/index.md` toctree.

Sources: `<aimrt>/document/sphinx-cn/tutorials/quick_start/`,
`<aimrt>/document/sphinx-cn/tutorials/index.md`.

### 4. Concepts and engineering conventions

The concept section documents:

- Modern CMake, FetchContent, selectable build options and exported Targets;
- AimRT's Module, Package, Application and Plugin concepts;
- the core design goals;
- the separation between business Interfaces and deployment/runtime Interfaces;
- API and configuration compatibility expectations;
- supported protocol families and TypeSupport.

Sources: `<aimrt>/document/sphinx-cn/tutorials/concepts/cmake.md`,
`concepts.md`, `core_design.md`, `interface.md`, `protocols.md` in the same
directory.

### 5. Public C++ Interface manual

AimRT provides one authored page for each major C++ surface:

- common conventions and reference-handle semantics;
- Runtime integration in App and Package modes;
- `CoreRef`;
- `ModuleBase`;
- Configurator;
- Executor, including asynchronous and coroutine forms;
- Logger;
- Parameter;
- Channel;
- RPC;
- Context.

These are usage manuals rather than just generated signatures: they explain
lifecycle availability, ownership, callback behaviour, configuration effects
and examples.

Sources: `<aimrt>/document/sphinx-cn/tutorials/interface_cpp/`, especially
`common.md` and `runtime.md`; the section ordering is declared in
`<aimrt>/document/sphinx-cn/tutorials/index.md`.

### 6. Public Python Interface manual

The Python section mirrors the applicable runtime and module concepts:
Runtime, `CoreRef`, `ModuleBase`, Configurator, Executor, Logger, Channel and
RPC. It deliberately omits interfaces that its Python binding does not expose.

Sources: `<aimrt>/document/sphinx-cn/tutorials/interface_py/`,
`<aimrt>/document/sphinx-cn/tutorials/index.md`.

### 7. Runtime configuration reference

AimRT documents its runtime YAML by component:

- common YAML shape and environment replacement;
- Module loading and per-Module configuration;
- Configurator;
- Plugin loading;
- main thread;
- guard thread;
- Executor types and scheduling;
- logging;
- Channel backends and selection rules;
- RPC backends and selection rules.

The common page supplies a full configuration skeleton and shared scheduling
concepts; each component page then defines its fields, defaults and examples.

Sources: `<aimrt>/document/sphinx-cn/tutorials/cfg/common.md`, the remaining
files under `tutorials/cfg/`, and
`<aimrt>/document/sphinx-cn/tutorials/index.md`.

### 8. Plugin documentation

There are 17 plugin pages: one development placeholder and pages for network,
MQTT, ROS 2, Parameter, time manipulation, log control, topic logging,
OpenTelemetry, record/playback, Zenoh, Iceoryx, gRPC, echo, proxy, service
introspection and AGI header plugins.

Most implementation pages explain build switches, runtime YAML, operational
behaviour and examples. The generic `how_to_dev_plugin.md` file itself is only
a short placeholder in this snapshot, so its presence should not be mistaken
for a complete plugin authoring guide.

Sources: `<aimrt>/document/sphinx-cn/tutorials/plugins/`,
`<aimrt>/document/sphinx-cn/tutorials/index.md`.

### 9. CLI and project generation

The CLI section documents installation forms and the available command. A
separate, substantially larger page explains project scaffolding configuration
and generated layouts.

Sources: `<aimrt>/document/sphinx-cn/tutorials/cli_tool/cli_tool.md`,
`<aimrt>/document/sphinx-cn/tutorials/cli_tool/gen_prj.md`.

### 10. Example catalogs

Three index pages enumerate the repository's C++, Python and Plugin examples.
Their value is navigation: users can find an example by Interface or plugin
instead of searching the source tree.

Sources: `<aimrt>/document/sphinx-cn/tutorials/examples/examples_cpp.md`,
`examples_py.md`, `examples_plugins.md`.

### 11. Troubleshooting and performance history

The miscellaneous section contains a FAQ and 11 versioned performance reports.
The reports retain benchmark data and figures for multiple releases rather than
showing only the latest result.

Sources: `<aimrt>/document/sphinx-cn/tutorials/misc/questions.md`,
`<aimrt>/document/sphinx-cn/tutorials/misc/performance_test/`.

### 12. Release notes and contact information

AimRT keeps an index plus 21 individual release-note pages, from `v0.6.0`
through `v1.8.0` in this checkout. The current release note separates important
features, smaller changes and bug fixes. The contact page describes expected
Issue contents, Pull Request workflow and a private contact address.

Sources: `<aimrt>/document/sphinx-cn/release_notes/index.md`,
`<aimrt>/document/sphinx-cn/release_notes/`,
`<aimrt>/document/sphinx-cn/contact/index.md`.

## What AsterCtrl already has under different names

The following are not missing; they are organized around AsterCtrl's domain
rather than AimRT's runtime-YAML model.

| AimRT concern | AsterCtrl location | Assessment |
| --- | --- | --- |
| Core design and basic concepts | [`concepts/architecture.md`](../concepts/architecture.md), [`concepts/runtime.md`](../concepts/runtime.md) | Present and more explicit about Linux/Zephyr Seams and deterministic lifecycle. |
| Deployment separation | [`concepts/graphs.md`](../concepts/graphs.md) | Present as first-class Application and Deployment Graphs rather than one process configuration file. |
| Protocol concepts | [`concepts/bounded-protobuf.md`](../concepts/bounded-protobuf.md) | Present and specific to MCU-safe bounded Protobuf and generated TypeSupport. |
| Packages and plugin boundaries | [`concepts/packages.md`](../concepts/packages.md), [`guides/plugins.md`](../guides/plugins.md) | Package/Core Plugin distinction is present; authoring details are not. |
| Build concepts | [`guides/build-and-ci.md`](../guides/build-and-ci.md) | Host/Zephyr build policy and CI are present, though exported Targets and consumption modes need reference-level detail. |
| C++ Hello World | [`tutorials/getting-started.md`](../tutorials/getting-started.md), [`tutorials/first-module.md`](../tutorials/first-module.md) | Present as a short checkout build and first Module walkthrough. |
| Environment switching | [`tutorials/real-and-sim.md`](../tutorials/real-and-sim.md) | Present as the real/simulation Adapter rule, but only as an eight-line summary. |
| Runtime configuration | [`concepts/graphs.md`](../concepts/graphs.md), [`guides/deployment.md`](../guides/deployment.md), JSON Schemas under `schemas/v1alpha2/` | The model exists and is stronger than a free-form YAML tutorial; the field reference is missing. |
| CLI | [`reference/cli.md`](../reference/cli.md) | All command families are listed, but command syntax, options and outputs remain too terse for a reference. |
| Troubleshooting | [`guides/debugging.md`](../guides/debugging.md) | A debugging starting point exists; a symptom-oriented FAQ does not. |
| Release process | [`reference/versioning.md`](../reference/versioning.md), [`reference/release-checklist.md`](../reference/release-checklist.md), [`development/index.md`](index.md) | Compatibility policy, release gates and development decisions exist; user-facing per-version release notes do not. |

AsterCtrl also has platform material that AimRT's documentation does not model:

- a dedicated Linux Runtime guide;
- a Zephyr Runtime and generated Kconfig/Devicetree guide;
- Hardware Profile and capability binding concepts;
- bounded MCU memory and link-budget resolution;
- official CAN/SocketCAN and USB CDC ACM transport behaviour;
- deployment bundle, staged activation and rollback guidance.

Sources: [`guides/linux.md`](../guides/linux.md),
[`guides/zephyr.md`](../guides/zephyr.md),
[`guides/transports.md`](../guides/transports.md),
[`guides/deployment.md`](../guides/deployment.md),
[`concepts/graphs.md`](../concepts/graphs.md).

## Gaps AsterCtrl should actually fill

### Priority 0: public Interface manual

This is the largest structural gap. AsterCtrl exposes a canonical C ABI and a
thin C++ facade under `src/interface/`, but the documentation has only a
lifecycle overview and one Module snippet. It needs an `interfaces/` section
with authored pages for:

- Interface conventions: borrowed handles, ownership, thread safety, lifecycle
  availability, ISR restrictions, error handling and ABI growth rules;
- C ABI: root tables, `struct_size`, `ASTER_ABI_VERSION`, string/byte views,
  callbacks, Package factories and Core Plugin factories;
- C++ `ModuleBase`, `ModuleRef`, `ModuleInfo` and `CoreRef`;
- `Status` categories and propagation;
- Configurator;
- Logger;
- Executor, task queues and delayed work;
- Channel publisher/subscriber and callback Context;
- unary RPC client/server, deadlines and cancellation limitations;
- Parameter, including the Zephyr v0.2 limitation;
- Clock and simulation clock substitution;
- Allocator and fixed-capacity rules;
- HardwareManager and capability acquisition;
- TypeSupport, bounded messages and schema identity.

Each page should state valid lifecycle phases, ownership/lifetime, concurrency
and ISR safety, failure Status values, platform differences and one compilable
example. Generated signatures cannot replace these behavioural contracts.

Evidence for the exposed surface:
`<asterctrl>/src/interface/aster_module_c_interface/aster_module_c_interface.h`,
`<asterctrl>/src/interface/aster_module_cpp_interface/`.

### Priority 0: Graph and Package schema reference

AsterCtrl has 12 Draft 2020-12 JSON Schemas under `schemas/v1alpha2/`, but the
Sphinx site has no one-to-one reference for them. Add a `configuration/`
section for:

- `workspace.yaml`;
- `module.yaml`;
- `application.yaml`;
- `deployment.*.yaml`;
- `hardware.*.yaml`;
- `inventory.yaml`;
- Package Manifest;
- `package.lock`;
- `deployment.lock.yaml`;
- deployment bundle and deployment state.

Every page should include a minimal complete document, field table, required
versus optional fields, defaults, identity/uniqueness rules, units, platform
constraints, validation failures and a link to the authoritative JSON Schema.
Provider selection, executor policy, QoS, capacity, route IDs, time authority,
Hardware bindings and inventory security boundaries need dedicated subsections.

Sources: `<asterctrl>/schemas/v1alpha2/`,
[`concepts/graphs.md`](../concepts/graphs.md),
[`guides/deployment.md`](../guides/deployment.md).

### Priority 0: complete end-to-end journeys

The current getting-started path proves a Linux checkout build, but it does not
yet carry a new user through the full product flow. Add task-oriented tutorials
for:

1. installing and consuming a released AsterCtrl SDK rather than a source
   checkout;
2. creating a Package and Module with `aster init`;
3. generating bounded Protobuf and wiring Channel and RPC ports;
4. resolving and running one Linux node;
5. building, flashing and observing `dev_c` or `mc02`;
6. connecting Linux and Zephyr over SocketCAN/CAN;
7. connecting Linux and Zephyr over USB CDC ACM;
8. switching the same Application between fake and physical providers;
9. producing and applying a Deployment Bundle, checking status and rolling
   back.

The existing four example families already provide the source basis, but the
documentation index does not expose them as a learning sequence.

Sources: `<asterctrl>/examples/linux_local/`,
`examples/provider_swap/`, `examples/linux_zephyr_can/`,
`examples/linux_zephyr_usb_cdc/`,
[`tutorials/getting-started.md`](../tutorials/getting-started.md).

### Priority 1: examples catalog

Add `examples/index.md` with a table containing purpose, platforms, transport,
Interfaces exercised, hardware required, build command and verification signal
for every maintained example. Link to the example README and relevant tutorial.
This small navigation page will make the existing example investment visible.

Source: `<asterctrl>/examples/`.

### Priority 1: Package and Core Plugin authoring guide

The current plugin guide defines boundaries but does not show how to implement,
manifest, build, export, load, validate and safely unload either plugin type.
Add separate Module Package and Core Plugin tutorials covering:

- the declarative Package Manifest;
- generated `ASTER_PKG_MAIN` glue;
- exported CMake Targets and installed-SDK consumption;
- factory ownership and reverse-order destruction;
- ABI/backend version negotiation;
- how the same Package becomes dynamic on Linux and static on Zephyr;
- which extensions may remain Linux-only;
- negative tests for missing symbols, ABI mismatch and live callbacks at unload.

Sources: [`concepts/packages.md`](../concepts/packages.md),
[`guides/plugins.md`](../guides/plugins.md),
`<asterctrl>/src/interface/aster_pkg_c_interface/`,
`<asterctrl>/src/interface/aster_core_plugin_interface/`.

### Priority 1: real CLI reference

Expand `reference/cli.md` into one page per command group or generate a stable
reference from the Click command definitions. For each command, record:

- full usage and arguments;
- whether the command is read-only, planning-only or mutating;
- input document kinds;
- generated files and exit codes;
- determinism and offline behaviour;
- representative success and failure examples.

The prose reference and `aster --help` should be checked for drift in CI.

Sources: [`reference/cli.md`](../reference/cli.md),
`<asterctrl>/tools/aster_cli/src/aster_cli/`.

### Priority 1: per-version release notes

The Development Log records implementation decisions and the release checklist
records gates; neither answers the user-facing question “what changed when I
upgrade?”. Add `release-notes/index.md` and one immutable page per published
version. Each release page should cover:

- highlights;
- breaking API, ABI and Schema changes;
- new platform/board/transport support;
- migration instructions;
- fixed defects;
- known limitations and hardware validation status;
- artifact/SBOM/checksum locations and rollback compatibility.

The first page should be `v0.2.0-alpha.1`; create later pages from actual tags,
not plans.

Sources: [`development/2026-09-04-v0.2-foundation.md`](./2026-09-04-v0.2-foundation.md),
[`reference/versioning.md`](../reference/versioning.md),
[`reference/release-checklist.md`](../reference/release-checklist.md),
`<asterctrl>/CHANGELOG.md`.

### Priority 1: generated API reference

AsterCtrl has no Doxygen configuration or generated public API site. Add
generated reference coverage for the four installed Interface Targets, but do
not copy AimRT's standalone HTML configuration unchanged. A better fit for the
current AsterCtrl site would be:

- Doxygen XML generated only from installed public headers;
- integration into the existing Sphinx navigation, for example through a
  Doxygen-to-Sphinx bridge;
- warnings treated as errors for public symbols;
- version derived from the release source of truth;
- no generated HTML committed to Git.

This recommendation is an architectural inference from AsterCtrl's current
single Sphinx site and strict CI. AimRT proves the value of API extraction but
not that a second disconnected HTML tree is required.

Sources: `<aimrt>/document/doxygen/`, `<aimrt>/CMakeLists.txt`,
`<asterctrl>/document/sphinx-en/conf.py`, `<asterctrl>/.github/workflows/ci.yml`,
`<asterctrl>/src/interface/`.

### Priority 2: troubleshooting and operational diagnosis

Expand the debugging guide into symptom-oriented procedures:

- schema or lock mismatch;
- an Instance that cannot be placed;
- missing or ambiguous Hardware capability;
- Runtime lifecycle rollback;
- Package ABI/symbol failure;
- executor queue exhaustion;
- CAN handshake, fragmentation, retry and peer-restart faults;
- USB framing and Linux TTY permissions;
- Zephyr size overflow, Devicetree readiness and board flashing;
- deployment digest/status disagreement.

The guidance should connect observable logs and CLI output to a corrective
action, not just describe the subsystem.

Sources: [`guides/debugging.md`](../guides/debugging.md),
[`guides/transports.md`](../guides/transports.md),
[`reference/testing.md`](../reference/testing.md).

### Priority 2: versioned performance and footprint reports

AimRT preserves performance reports per release. AsterCtrl should eventually do
the same, but with embedded-relevant dimensions:

- Local Channel and RPC latency distributions;
- executor wake-up latency and jitter;
- CAN utilization, loss/retry and reassembly behaviour;
- USB framing throughput;
- fixed Runtime RAM by capacity choice;
- firmware flash/RAM for `native_sim`, `dev_c` and `mc02`;
- build-time and artifact-size regression methodology.

Do not add a performance directory filled with targets or estimates. Publish it
only when the benchmark harness, environment and raw results are reproducible.

Sources for the comparison pattern:
`<aimrt>/document/sphinx-cn/tutorials/misc/performance_test/`.
Source for AsterCtrl's current test dimensions:
[`reference/testing.md`](../reference/testing.md).

### Priority 2: Chinese documentation and version selection

AsterCtrl's Sphinx configuration declares English and has one source tree.
AimRT maintains complete Chinese and English trees plus a Git-tag version
selector. AsterCtrl should support Chinese because its initial contributor and
user context requires it, but duplicating every early-alpha page immediately
would create drift.

Recommended order:

1. stabilize the English structure and terminology;
2. define one canonical-language and translation-review policy;
3. add Chinese navigation and translate Priority 0/1 user paths first;
4. enable version switching at `v0.2.0`, when there is a stable tag worth
   retaining alongside `latest`.

Sources: `<aimrt>/document/sphinx-cn/`, `<aimrt>/document/sphinx-en/`,
`<aimrt>/document/sphinx-cn/conf.py`,
`<asterctrl>/document/sphinx-en/conf.py`.

## AimRT structures AsterCtrl should not copy now

### Python Runtime Interface pages

AimRT has a Python Runtime and therefore needs a Python Interface manual.
AsterCtrl's Python package is the `aster` CLI implementation, not an Application
Module Runtime API. Creating parallel Python Module documentation now would
describe a product that does not exist.

Sources: `<aimrt>/document/sphinx-cn/tutorials/interface_py/`,
`<asterctrl>/tools/aster_cli/`.

### AimRT-specific plugin pages

ROS 2, MQTT, Zenoh, Iceoryx, gRPC, OpenTelemetry and record/playback pages are
appropriate because those implementations exist in AimRT. AsterCtrl explicitly
defers ecosystem bridges and several transports. It should document a plugin
only after the plugin exists and has a supported manifest/configuration surface.

Sources: `<aimrt>/document/sphinx-cn/tutorials/plugins/`,
[`guides/transports.md`](../guides/transports.md),
[`development/2026-09-04-v0.2-foundation.md`](./2026-09-04-v0.2-foundation.md).

### A single AimRT-style runtime YAML hierarchy

AimRT's `cfg/` pages describe one process configuration rooted at `aimrt:`.
AsterCtrl intentionally separates logical Application, physical Deployment,
Hardware and Inventory concerns. Copying AimRT's `main_thread`, `guard_thread`,
`channel` and `rpc` node hierarchy would collapse that boundary. Equivalent
information belongs in the Deployment Schema reference and resolved-lock
documentation.

Sources: `<aimrt>/document/sphinx-cn/tutorials/cfg/common.md`,
[`concepts/graphs.md`](../concepts/graphs.md).

### Windows build documentation

AimRT keeps a Windows source-build page. AsterCtrl v0.2 targets Linux and
Zephyr. A Windows page should wait for a declared, tested support level.

Sources: `<aimrt>/document/sphinx-cn/tutorials/quick_start/build_from_source_windows.md`,
[`concepts/architecture.md`](../concepts/architecture.md),
[`guides/build-and-ci.md`](../guides/build-and-ci.md).

### Documentation-serving Docker images

AimRT has separate Nginx packaging under both Doxygen and Sphinx. AsterCtrl
already pins its documentation dependencies in `uv.lock` and CI builds Sphinx
with warnings as errors, checks links and checks spelling. Unless a standalone
documentation deployment specifically requires Nginx images, duplicating this
infrastructure would add another release surface without improving content.

Sources: `<aimrt>/document/doxygen/dockerfile/`,
`<aimrt>/document/sphinx-cn/dockerfile/`, `<asterctrl>/pyproject.toml`,
`<asterctrl>/uv.lock`, `<asterctrl>/.github/workflows/ci.yml`.

### AimRT's configuration details verbatim

Several details in the inspected AimRT snapshot show why its pattern should be
adapted rather than copied mechanically: the Sphinx metadata reports `v1.7.0`
while the release-note tree contains `v1.8.0`; Doxygen's `PROJECT_NUMBER` is
`0.10.0`; and Doxygen warnings are not errors. AsterCtrl already checks Sphinx
warnings and links in CI and verifies release metadata, which is the stronger
baseline to keep.

Sources: `<aimrt>/document/sphinx-cn/conf.py`,
`<aimrt>/document/sphinx-cn/release_notes/v1_8_0.md`,
`<aimrt>/document/doxygen/Doxyfile`,
`<asterctrl>/.github/workflows/ci.yml`,
`<asterctrl>/scripts/ci/verify_alpha_tag.py`.

## Recommended target tree

This structure retains AsterCtrl's current strengths while filling the actual
user-facing gaps:

Each authored language has this structure. The English tree is shown below and
the Chinese tree mirrors it under `document/sphinx-cn/`.

```text
document/sphinx-en/
├── index.md
├── getting-started/
│   ├── installation.md
│   ├── linux-first-node.md
│   ├── zephyr-first-board.md
│   └── linux-zephyr-first-link.md
├── concepts/
│   ├── architecture.md
│   ├── graphs.md
│   ├── runtime.md
│   ├── packages.md
│   └── bounded-protobuf.md
├── interfaces/
│   ├── overview.md
│   ├── c-abi.md
│   ├── module-and-core.md
│   ├── status-and-context.md
│   ├── configurator.md
│   ├── logger.md
│   ├── executor.md
│   ├── channel.md
│   ├── rpc.md
│   ├── parameter.md
│   ├── clock-and-allocator.md
│   ├── hardware-manager.md
│   └── type-support.md
├── configuration/
│   ├── overview.md
│   ├── workspace.md
│   ├── module.md
│   ├── application.md
│   ├── deployment.md
│   ├── hardware.md
│   ├── inventory.md
│   ├── package.md
│   ├── locks.md
│   └── bundle-and-state.md
├── tutorials/
│   ├── first-module.md
│   ├── real-and-sim.md
│   ├── channel-and-rpc.md
│   ├── can-cross-node.md
│   ├── usb-cross-node.md
│   └── deployment-roll-back.md
├── guides/
│   ├── build-and-ci.md
│   ├── linux.md
│   ├── zephyr.md
│   ├── transports.md
│   ├── debugging.md
│   └── plugin-development.md
├── examples/
│   └── index.md
├── reference/
│   ├── cli/
│   ├── api/
│   ├── testing.md
│   ├── versioning.md
│   └── release-checklist.md
├── release-notes/
│   └── index.md
└── development/
    ├── index.md
    └── ...
```

`development/` should remain engineering history and architectural decisions.
`release-notes/` should remain the immutable user-facing upgrade history. They
serve different audiences and should not be merged.

## Suggested implementation order

1. Add the `configuration/` index and generate field tables from the existing
   JSON Schemas, then write the semantic constraints that JSON Schema cannot
   express.
2. Add `interfaces/overview.md`, C ABI, Module/Core, Status, Channel, RPC and
   Executor pages; cover remaining services immediately afterward.
3. Add `examples/index.md` and turn the four existing example families into a
   deliberate learning path.
4. Expand the Linux/Zephyr quick starts through build, run/flash and observable
   success.
5. Add Package/Core Plugin authoring and installed-SDK consumption.
6. Publish the first release-note page from the real alpha release metadata.
7. Add generated API extraction, then make undocumented public symbols a CI
   failure.
8. Add Chinese translations and version selection once the Priority 0/1
   structure is stable.
9. Add reproducible performance and footprint reports after the benchmark
   harness is part of the release process.

This ordering makes the documentation useful before making the documentation
system elaborate.
