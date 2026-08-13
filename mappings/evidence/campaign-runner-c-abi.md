# Campaign runner C ABI boundary

`campaign_runner` is a lifecycle-ABI-v1 plugin. Its only DLL export is
`SmedleyPluginGetApiV1`; it has no imports from `smedley_kernel.dll`.

At load it dynamically resolves the v1 logging, campaign runtime, campaign
automation, and event-services tables, then opens a campaign session. The
runner retains save selection, observer policy, retry limits, benchmarks,
telemetry, and quit decisions. The kernel retains native access and validates
all copied records and mutations.

Campaign automation and campaign-console callbacks do not log or call a
service. They copy bounded input into atomics and return a fixed response. The
owner timer drains that work and performs the corresponding policy and service
calls. This prevents reentrant hook paths from retaining engine data or
performing lifecycle operations.

Host builds audit the exact export set and require zero kernel imports. They do
not prove that the supported Victoria II executable accepts every native hook
and campaign transition; that remains the live-game validation gap.
