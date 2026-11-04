"""Evidence the consumer keeps around the harness. Sketch only, not a Crux interface."""
import hashlib
import json
import platform
from pathlib import Path

import pytest


@pytest.fixture(scope="session")
def image_identity(twister_harness_config):
    """Record what ran before it runs: the harness itself records no image identity."""
    device = twister_harness_config.devices[0]
    image = Path(device.app_build_dir) / "zephyr" / "zephyr.exe"
    identity = {
        "image": str(image),
        "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "device_type": device.type,
        "host": platform.platform(),
    }
    (Path(device.build_dir) / "consumer-identity.json").write_text(json.dumps(identity, indent=2))
    return identity
