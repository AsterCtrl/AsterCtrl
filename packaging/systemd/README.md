# systemd deployment

`aster-node@.service` runs one staged Linux node from
`/opt/aster/<node>/current`. A Deployment Bundle owns the versioned directories
and changes `current` only after its digest and Deployment Lock have been
verified. The service account must be granted access to only the SocketCAN or
TTY devices declared by that node's Hardware Profile.

The unit is a packaging template. Integrators may override the artifact root or
device policy with a systemd drop-in; Application Modules must not embed those
host details.
