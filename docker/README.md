# kas image variants

## kas-icecc

Extends `ghcr.io/siemens/kas/kas:4.7` with the `icecc` client binary so
Yocto's `icecc.bbclass` (which uses `HOSTTOOLS_NONFATAL += "icecc"`) can
find a compiler driver and dispatch jobs to the network scheduler.

### Build

```sh
docker build -t kas-icecc:4.7 -f docker/kas-icecc.Dockerfile docker/
```

### Use

```sh
KAS_BUILD_DIR=$PWD/build \
KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container \
    --runtime-args "--network=host" \
    --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
    build kas/base.yml
```

- `--runtime-args` is a CLI flag, not `KAS_RUNTIME_ARGS` env — the env var
  is overwritten by `kas-container` (script line 232).
- `--network=host` lets the local iceccd (which the container talks to) see
  the scheduler on the LAN (default ports 8765 scheduler, 10245 daemon)
  without bridge NAT.
- `-v /run/icecc:/var/run/icecc:rw` is the key: it bind-mounts the local
  iceccd's Unix socket into the container so the icecc client inside the
  container can dispatch jobs to the local daemon, which then talks to the
  scheduler. Without this mount the client silently falls back to local
  builds.

### Requires

- `iceccd` running on local host (so local jobs are counted)
- `icecc-scheduler` + `iceccd` running on 10.0.10.45
- Both daemons configured with `ICECC_NETNAME="banks-yocto"` so they
  only join this build farm.
