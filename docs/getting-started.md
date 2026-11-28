# Getting started

This page sets up the environment and takes you to a first working build, on native Linux or on Windows with WSL2. It states what the scripts and the Dev Container require, so that the first build does not fail for a reason that is not obvious. The [Tutorial](tutorial.md) continues from here, and the [Build and Integration Guide](build.md) covers the builds in depth.

## Prerequisites

Git, Docker, VS Code with the Dev Containers extension, and 30 to 40 GiB of free disk (see [Disk and memory](#5-disk-and-memory)). On Windows, use WSL2 ([section 3](#3-windows-with-wsl2)). Network access to the hosts in [section 4](#4-network-and-proxy-prerequisites).

## 1. Use a Git clone

The scripts find the repository with `git rev-parse --show-toplevel`. Nearly every script in `scripts/` does this, and `scripts/check_docs.py` uses `git ls-files`. Crux must therefore be a Git working tree. Without `.git`, the first command stops with `fatal: not a git repository`.

A real clone, made inside Linux or WSL2, is the recommended form. It keeps the executable bits and the LF line endings:

~~~sh
mkdir -p ~/crux-workspace
git clone <repository-url> ~/crux-workspace/crux
~~~

An archive is a fallback with these caveats. If you received one, make it a Git repository from its root:

~~~sh
git init -q && git add -A && git commit -qm import
~~~

In a fresh WSL2 distribution set `git config --global user.name` and `user.email` first, or the commit fails.

Caveats of an archive:

- The executable bits that Git records (`100755`) may be lost, and then `scripts/...` fails with "Permission denied". `chmod +x scripts/*.sh` covers only part of them, because other files are executable too. Compare with a clone (`git ls-files -s | awk '$1==100755'` in a real clone lists them) or clone again.
- CRLF files in an archive stay CRLF. `.gitattributes` applies only to a fresh checkout, so a copy that already holds CRLF files has to be cloned again instead of converted by hand.

## 2. Folder layout

Three things are involved, and each has one owner:

| Who | Creates |
| --- | --- |
| You | The parent folder, with `mkdir -p`, and the clone inside it. The parent folder can have any name, for example `~/crux-workspace`. Use a folder that holds only Crux |
| The Dev Container | Nothing in the parent folder. It mounts the parent folder (`workspaceMount` is `${localWorkspaceFolder}/..` in [`devcontainer.json`](../.devcontainer/devcontainer.json)) at `/workspace` and opens the repository inside it |
| `scripts/bootstrap.sh`, which the Dev Container runs (through `scripts/bootstrap-zephyr.sh`) | `.west/`, `zephyr/` and `modules/` in the parent folder. It runs `west init -l` there and then `west update`. `bootloader/` appears only when the optional west group `crux-boot` is enabled (for example by `scripts/test-boot-composition.sh`), because `west.yml` filters that group out by default |

The script stops with `Expected repo at <workspace>/crux` unless the repository folder is named exactly `crux`. So:

- Name the clone folder `crux`. A folder such as `crux-main` does not work. An existing clone in a folder named `crux-pilot` still works for now.
- Make the parent folder an empty folder that is used only for Crux.
- Do not clone directly into your home directory. Its parent would be your whole home directory, which would be mounted into the container and filled with west checkouts.

One layout, on the host and in the container:

~~~text
~/crux-workspace/          # on the host: you create it; becomes the west workspace
  crux/                    # you clone the repository here
  .west/  zephyr/  modules/    # created by the bootstrap (bootloader/ only with the crux-boot group)

/workspace/                # the same folder inside the Dev Container
  crux/  zephyr/  modules/    # never /workspace/crux/crux
~~~

Open `~/crux-workspace/crux` in VS Code and choose **Dev Containers: Reopen in Container**. There is one Dev Container, for work with and without a board ([section 6](#6-the-dev-container-sets-itself-up)). The container's pinned Ubuntu 24.04 amd64 environment, its tools and the arm64 caveat are described in the [Build and Integration Guide](build.md#development-environment).

## 3. Windows with WSL2

- Check with `wsl -l -v` in PowerShell that the distribution shows `VERSION` `2`, because WSL 1 has no USB passthrough and Docker's WSL integration needs WSL 2 ([Comparing WSL versions](https://learn.microsoft.com/en-us/windows/wsl/compare-versions), [Connect USB devices](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)).
- Use Docker Engine inside the WSL2 distribution; the board route needs it. Docker Desktop with the WSL2 backend may also work without a board; the Dev Container's `/dev/bus/usb` binding has not been tried with it on Windows.
- Clone inside the WSL2 file system, for example under `~/crux-workspace`. Do not clone under `/mnt/c`. A bind mount from the Windows drive is slow for Zephyr builds, and file modes and line endings are not reliable there.
- Set `git config --global core.autocrlf false` in WSL2 before cloning. `.gitattributes` keeps LF line endings for shell scripts and the other files that run in Linux.
- In VS Code on Windows, turn off **Dev › Containers: Mount Wayland Socket** in your user settings (it cannot be set in the repository). The container needs no display, and with the setting on it can fail to start with `bind source path does not exist: /run/user/1000/wayland-0`; after turning it off, run **Dev Containers: Rebuild Container** once.
- Start VS Code from WSL2 (`code ~/crux-workspace/crux`) with the Dev Containers extension, then reopen in the container.
- Give the WSL2 virtual machine more than the container limit (see section 5). Memory and processors for WSL2 are set in `%UserProfile%\.wslconfig`.

## 4. Network and proxy prerequisites

The first set-up downloads from these places. Each one has to be reachable, directly or through a proxy.

| What | Where from | Used by |
| --- | --- | --- |
| Dev Container base image | `mcr.microsoft.com` (`.devcontainer/Dockerfile`) | Building the container |
| Ubuntu packages | The Ubuntu archive | The Dockerfile (`apt-get`) |
| Python packages | PyPI | The Dockerfile's own `pip install` (west, pytest), the tools step of `scripts/bootstrap.sh`, and the pip installs in `scripts/bootstrap-zephyr.sh` (Zephyr requirements) and `scripts/test-boot-composition.sh` (MCUboot requirements) |
| west projects | `github.com` (`zephyrproject-rtos` by default; the `wolfSSL` and `tuxera` projects only when their optional west groups are enabled, see `west.yml`) | `west update` in `scripts/bootstrap-zephyr.sh` |
| GoogleTest | A source archive from `github.com/google/googletest`, fetched by CMake (`CMakeLists.txt`) | The first Host configure, in the host step of `scripts/bootstrap.sh` (`scripts/bootstrap-host.sh`) |
| Zephyr SDK | Zephyr SDK release assets on GitHub. `west sdk install` looks the release up through the GitHub REST API | The zephyr step of `scripts/bootstrap.sh` (`scripts/bootstrap-zephyr.sh --install-sdk`) |
| J-Link software | `www.segger.com` (the pinned version, downloaded by the image build unless a placed package or `CRUX_JLINK_URL` overrides it, or `CRUX_JLINK=0` skips it) | The image build ([board route](board-jlink-usbipd.md#steps), step 3) |

What the code does about the network:

- `scripts/check-host.sh` runs with dependency resolution off, so it needs the host step of the bootstrap to have fetched GoogleTest.
- `scripts/bootstrap-zephyr.sh` reads `CRUX_GITHUB_TOKEN`. When it is set, the script passes it to `west sdk install` as `--personal-access-token`. Use it when the unauthenticated GitHub API rate limit stops the SDK download:

  ~~~sh
  export CRUX_GITHUB_TOKEN=<read-only token>
  scripts/bootstrap.sh zephyr
  ~~~

- A proxy allowlist may also need `api.github.com`, `codeload.github.com` and the hosts of the SDK release files.
- The scripts set no proxy. Behind a proxy, configure Docker's proxy settings for the image build, and pass `HTTP_PROXY`, `HTTPS_PROXY` and `NO_PROXY` into the container yourself. No proxy set-up has been run for this guide.
- If the proxy re-signs TLS, downloads in the image build or in the container fail with `curl: (60) SSL certificate problem: unable to get local issuer certificate` (or a similar certificate error from pip, Git or west). Put your organisation's root CA, as a PEM or DER `.crt` file, in `.devcontainer/certs/` on your machine, never in a commit, and rebuild the container. [`.devcontainer/certs/README.md`](../.devcontainer/certs/README.md) gives the steps.

## 5. Disk and memory

These figures are estimates. No minimum has been measured.

- **Container limits.** `devcontainer.json` starts the container with `--memory=4g` and `--cpus=2`. Docker Desktop or WSL2 must offer at least that. Full Zephyr suites are slow at 2 CPUs; a draft pull request runs them in CI instead.
- **Disk.** Plan for 30 to 40 GiB free before the SDK installation, and more if you keep several build directories. One early run went from 41 GiB to 31 GiB free after the image, the SDK and the west workspace, and to 26 GiB after an image rebuild for the sanitizer runtime ([baseline](baseline.md)). Builds were smaller then, so treat the 10 GiB drop as a lower bound.
- **WSL2.** The Docker disk lives in a virtual disk file that grows and does not shrink by itself. Check the free space of the Windows drive that holds it.
- **Where it goes.** The SDK, the Python virtual environment and the caches are in the Docker volume `crux-host-cache`, mounted at `/opt/crux-cache`. The west checkouts are in the parent folder of the repository. Builds go to `build/` in the repository, which Git ignores.

## 6. The Dev Container sets itself up

After the container is created, it runs `scripts/bootstrap.sh` by itself (`postCreateCommand` in `devcontainer.json`). You do not run a set-up command. In order, it:

1. adds the west workspace folders to Git's `safe.directory` list and installs the pinned CMake, Ninja, clang-format, west and pytest into a Python environment in the cache volume;
2. configures, builds and tests the Linux Host preset `host-debug` (`scripts/bootstrap-host.sh`), which fetches GoogleTest once;
3. creates the west workspace in the parent folder, installs Zephyr's Python requirements and the Zephyr SDK with the `arm-zephyr-eabi` toolchain (`scripts/bootstrap-zephyr.sh --install-sdk`), then writes west's bash Tab completion; a failure there only prints a warning.

A step that finished is skipped the next time, so reopening or rebuilding the container is quick; a step whose inputs change (the tool pins, `west.yml`, a deleted build folder, a deleted cache volume) runs again. Each step writes its output to `/opt/crux-cache/bootstrap/<step>.log`.

If a download fails, the container still opens. The terminal shows which step failed, its log, and the command that runs it again. Each time VS Code attaches, `scripts/bootstrap.sh status` prints the state of every step. From `/workspace/crux`:

~~~sh
scripts/bootstrap.sh status      # ok, running, needs rerun, FAILED or not run, with the command to rerun it
scripts/bootstrap.sh             # run every unfinished step again
scripts/bootstrap.sh zephyr      # one step: host or zephyr
CRUX_BOOTSTRAP_FORCE=1 scripts/bootstrap.sh   # run every step again, finished or not
~~~

To set up less, set `CRUX_BOOTSTRAP` to `host` or `zephyr` in the shell that starts VS Code before the container is created; the default is `all`. Host work needs only `host`; the Zephyr samples on `native_sim` and a board need `zephyr`.

| What you want | Needs | Then |
| --- | --- | --- |
| Host tests and samples | The host step | Part A of the [Tutorial](tutorial.md) |
| Zephyr samples on `native_sim`, Twister, board cross-builds | The zephyr step. The west checkouts and the SDK are large downloads | Part B of the [Tutorial](tutorial.md#part-b-zephyr) |
| A board | Both steps, the J-Link package and the Linux side ([section 7](#7-if-you-use-a-board)) | Part C of the [Tutorial](tutorial.md#part-c-run-on-your-board) |
| Before a pull request | The host step, because `scripts/check-host.sh` runs offline and reuses the GoogleTest that it fetched | `scripts/check-host.sh`, then the [test guide](../tests/README.md#before-you-open-a-pull-request) |

Disk and time of the steps have not been measured. None of these commands programs or resets a board.

### Open and rebuild the container

Run these commands from the VS Code Command Palette:

- If VS Code asks to rebuild the container, choose **Rebuild**.
- After you take a new version, or after you delete and recreate the folder, run **Dev Containers: Rebuild Container**. Deleting the folder keeps the container and the cache volume.

## 7. If you use a board

Most work needs no board. Host tests, `native_sim` runs and board cross-builds run without one, and none of them proves physical operation.

To run Crux on a board you need the board's debug probe and a USB connection for its console. The page for your board under [Boards](boards/README.md) says which probe, which connectors and which host have been used, and holds every board procedure. Any operation on a board needs approval for the task. [Part C of the Tutorial](tutorial.md#part-c-run-on-your-board) builds a sample for your board and flashes it.

**The container can open the host's USB devices.** So that a debug probe works without a second configuration, the Dev Container binds the host's `/dev/bus/usb` and allows USB character devices (`--device-cgroup-rule=c 189:* rmw`); it does not use `--privileged`.

The `vscode` user can use `sudo`, so a process in the container can open any USB device of the host, such as a keyboard, a security key or a storage device, whether or not you have a board.
Use it with code you trust.

If a security policy does not allow this, a configuration without the USB binding and the cgroup rule can be added back next to this one; Host and `native_sim` work need neither.

For a SEGGER J-Link on Windows with WSL2, the Dev Container is already set up for the probe and its build installs the J-Link software; the attach is yours, and two more steps are optional or need only a check:

1. In PowerShell on Windows (install [usbipd-win](https://github.com/dorssel/usbipd-win) first; `bind` needs an administrator PowerShell, once per device):

   ~~~powershell
   usbipd list
   usbipd bind --busid <BUSID>
   usbipd attach --wsl --busid <BUSID>
   ~~~

   `usbipd list` shows each device's `BUSID` and `STATE`; the J-Link has vendor ID `1366`. Run `bind` only when the STATE is `Not shared` (it persists across reboots); skip it when `Shared`; with `Attached` there is nothing to do. `attach` needs no administrator after the bind ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)).

2. Only where the container cannot open the probe, for example on native Linux without the udev rule: in a shell on the host, in the repository checkout (not in the container), run `scripts/bootstrap.sh board-host`. It installs `usbutils` and the udev rule and checks the probe's device node.

   ~~~sh
   scripts/bootstrap.sh board-host
   ~~~

3. Optional: nothing to do for the J-Link software.
   Every Dev Container build, with or without a board, downloads the J-Link version pinned in the Dockerfile from SEGGER (about 75 MB) and installs it only if its SHA-256 matches.

   **That download accepts SEGGER's terms of use for you.**
   To use your own copy instead, put `JLink_Linux_<version>_x86_64.deb` in `.devcontainer/jlink/packages/`, or set `CRUX_JLINK_URL` and `CRUX_JLINK_DEB_SHA256` to a copy your organisation hosts, then run **Dev Containers: Rebuild Container**; the build argument `CRUX_JLINK=0` skips the J-Link software.
   CI and other automated image builds always pass `--build-arg CRUX_JLINK=0` ([CI and automated image builds](board-jlink-usbipd.md#ci-and-automated-image-builds)).

The attach and the host set-up stay manual because nothing inside the container can do them: `usbipd` runs on Windows and `bind` needs an administrator; the udev rule belongs to the Linux system that owns the probe and needs `sudo` there, which the container's automatic set-up cannot ask for. After that, building, flashing and debugging run from VS Code tasks and debug profiles ([J-Link board route](board-jlink-usbipd.md)).

On native Linux skip step 1. The [J-Link board route](board-jlink-usbipd.md) has the checks, the flash and what to do when a step fails. If the J-Link download fails, the build finishes without the J-Link software and the container works for everything except flashing and debugging; `scripts/bootstrap.sh status` says how to add a package, and **Dev Containers: Rebuild Container Without Cache** tries the download again (a plain rebuild can reuse the cached step).

## 8. Supported environments

| Environment | Scope |
| --- | --- |
| Linux Host (amd64) | Development and tests, including sanitizer builds and source-tree consumers. Not an Embedded Linux product variant |
| Zephyr `native_sim` | Zephyr component and integration tests without hardware |
| MIMXRT1060-EVKB Rev B (`mimxrt1060_evk@B//qspi`) | First physical target. Results are limited to the documented firmware and configurations |
| RT1170-EVK Rev A1, Cortex-M7 | Software only: cross-build, probe identification and review. Not verified on hardware |
| Embedded Linux | Not implemented. It will be designed at the service boundary when a first product needs it |

A Host test, a `native_sim` run, a target cross-build and a physical-board run each prove different things, and none implies another. Each board has its own page under [Boards](boards/README.md). [Component readiness](component-readiness.md) records which of them exist for each component.
