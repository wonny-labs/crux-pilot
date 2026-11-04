"""Errors raised by the device-control tool."""


class DeviceControlError(Exception):
    """Base class: the tool refused or failed an operation."""


class ProfileError(DeviceControlError):
    """The board profile is missing, malformed or unsafe."""


class ManifestError(DeviceControlError):
    """The manifest is malformed, or an artefact or the board does not match it."""


class EvidenceExistsError(DeviceControlError):
    """The evidence directory already exists."""


class BoardLockHeld(DeviceControlError):
    """Another run holds this board."""


class GateRefused(DeviceControlError):
    """A hardware operation was not allowed, or the profile cannot perform it."""


class ToolFailed(DeviceControlError):
    """An external probe or flash tool failed or timed out."""


class ConsoleError(DeviceControlError):
    """The console could not be opened or used."""


class ConsoleTimeout(ConsoleError):
    """The expected console output did not arrive in time."""

    def __init__(self, message, received=b""):
        super().__init__(message)
        self.received = received


class IdentityMismatch(DeviceControlError):
    """The identity printed by the application does not match the manifest."""
