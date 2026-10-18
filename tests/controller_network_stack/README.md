# Native socket cleanup regression

Run through `scripts/test-m3.sh` in the Linux development environment, or select
this directory with Twister on `native_sim/native/64`. The fixture executes the
production native client with real Zephyr sockets and a local loopback peer;
there are no socket or clock mocks and no host network interface is configured.

Each scenario verifies ten TCP and ten UDP request frames at the peer. Echo
expects a successful round; TCP closure expects TCP EOF with UDP echo; silence
withholds both responses and expects finite timeouts. The immutable idle baseline
includes two fixture listeners. Seven contexts account for the four production
client slots plus peer listeners/accepted connection; the larger fixture buffer
pools are explicit test overhead, not EVKB resource qualification.

Listeners remain alive until client cleanup finishes. Each case checks that all
contexts and configured packet/data pools return to idle after peer shutdown.
Logs retain per-exchange results, cleanup durations and resource availability.
The 90-second Twister timeout is a wall-clock hang guard for the simulated suite;
it is not proof of the production 150-second worst-case round budget or of
physical link-loss recovery. Controlled deadline boundary tests are separate in
`tests/controller_network`.
