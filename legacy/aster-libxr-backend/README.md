# xrobot-libxr-backend

Adapters from portable Runtime seams to libxr time, I/O, queues, tasks and diagnostics.

`CanAdapter` converts the transport's classic-CAN frame contract to
`LibXR::CAN`. Receive callbacks only timestamp and enqueue frames in a bounded
SPSC queue; route decoding and application publication happen later when the
Runtime-owned receive executor calls `Drain`. The adapter and its libxr CAN
object therefore have node lifetime and are initialized before executors start.
The supplied clock callback must be safe in the CAN receive interrupt context.
Because libxr CAN subscriptions currently have node lifetime and no unregister
operation, an initialized adapter must outlive its `LibXR::CAN` subscription.
