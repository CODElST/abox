# ABox packaging

## Installation rules

ABox uses standard CMake installation directories. With
`-DCMAKE_INSTALL_PREFIX=/usr`, the executables are installed as
`/usr/bin/abox` and `/usr/bin/aboxd`. The configuration template is installed
as `/etc/abox/abox.conf`; documentation is installed below the configured
documentation directory.

The optional systemd unit is enabled at configure time with
`-DABOX_INSTALL_SYSTEMD=ON`. It is installed but never enabled or started by
the build, package generation, or tests.

## Build and package

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release -j
cpack --config build-release/CPackConfig.cmake -G DEB -B dist
cpack --config build-release/CPackConfig.cmake -G TGZ -B dist
```

The package does not contain an Ubuntu root filesystem. Run `sudo abox setup`
after reviewing its diagnostic plan to provision one explicitly.

## Staged installation and validation

Before changing the host, use:

```bash
DESTDIR="$PWD/stage" cmake --install build-release --prefix /usr
dpkg-deb --info dist/*.deb
dpkg-deb --contents dist/*.deb
```

The `install_staging` and `deb_package_validation` CTest tests perform the
corresponding automated checks when the required host packaging tools exist.

## Filesystem and security notes

The shared default root filesystem is not a per-sandbox writable filesystem.
It does not provide copy-on-write storage or storage quotas. The daemon and
one-shot runner have different isolation paths; the daemon currently manages
cgroup-backed host-namespace workloads. The optional service assumes an
`abox` system user and must be reviewed with the host's systemd/cgroup
delegation policy before enabling it.
