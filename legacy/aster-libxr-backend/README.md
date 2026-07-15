# xrobot-libxr-backend

Adapters from portable Runtime seams to libxr time, I/O, queues, tasks and diagnostics.

`LibxrClassicCanEndpoint` owns one libxr subscription and fans standard frames
out through a fixed subscription table. Motor, power, and inter-board adapters
can therefore share one physical CAN without owning HAL callbacks or allocating
after initialization.

`CanAdapter` converts the transport's classic-CAN frame contract through that
endpoint. Receive callbacks only timestamp and enqueue frames in a bounded
SPSC queue; route decoding and application publication happen later when the
Runtime-owned receive executor calls `Drain`. The adapter and its libxr CAN
endpoint therefore have node lifetime and are initialized before executors start.
Construction only needs the endpoint, so its `writer()` can be
passed into generated node composition. The resulting node receiver is then
attached exactly once with `BindReceiver()` before `Initialize()` registers the
endpoint subscription; this ordering breaks the writer/receiver construction
cycle. The endpoint clock callback must be safe in the CAN receive interrupt
context. Because subscriptions have node lifetime and no unregister operation,
an initialized endpoint must outlive every registered receiver.

`CanAdapterStats` exposes RX/TX counts, invalid/drop/dispatch failures, and a
lock-free RX queue high-water mark so target load can be diagnosed without
logging from the receive callback.
