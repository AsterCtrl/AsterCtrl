Versioning and compatibility
============================

Releases use Semantic Versioning. C ABI structures carry an ABI version and
structure size. YAML documents carry ``api_version`` and ``kind``. Message
compatibility is identified by a canonical descriptor hash, and resolved
deployments record every type, route and artifact digest.

Schema v1alpha2 is intentionally incompatible with the v0.1 experimental
schemas. No compatibility shim for ``asterctl`` or ``aster_tools`` is provided.
