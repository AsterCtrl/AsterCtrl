# Alpha rollback

Linux installations are staged under a versioned prefix. Stop the AsterCtrl
node, restore the previous prefix and deployment lock, verify its checksum, and
restart the previous systemd unit.

For Zephyr, keep the last known-good signed firmware and matching
`deployment.lock.yaml`. Flash both together through the board's configured
runner; do not mix firmware and locks from different releases.

An alpha rollback does not change or archive the v0.1 repositories. If the new
runtime cannot load its package set or rejects the lock/schema hash, return to
the previous complete bundle rather than selectively copying libraries.
