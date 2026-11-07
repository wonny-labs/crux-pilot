// The English text of the network settings and restart screens, as the Preact example words it, so that the
// two examples say the same thing and the shared browser cases hold for both. The error table holds the
// codes that these two screens meet. The Preact table also holds `aborted`, which Angular never reports. A name from the device is looked
// up with `lookup`, which does not reach built-in properties such as constructor.
import type { Failure } from './api';

export const NETWORK = {
  title: 'Network settings',
  navigation: 'Network',
  running: 'Running now',
  saved: 'Saved for the next boot',
  address: 'IPv4 address',
  prefix: 'Prefix length',
  gateway: 'Gateway',
  gatewayHint: 'Optional. Leave it empty for no gateway.',
  noGateway: 'None',
  noAddress: 'This interface has no IPv4 address at the moment.',
  nothingSaved: 'Nothing is saved, so the next boot uses the default values.',
  sourceSaved: 'The running values came from saved settings.',
  sourceDefault: 'The running values are the defaults of this firmware.',
  rebootRequired: 'The saved values differ from the running ones. They take effect at the next boot.',
  rebootLink: 'Restart the device',
  noAddressAfterBoot: 'A restart is not promised to restore an address.',
  savePending: 'A save is running on the device.',
  saveFailedState: 'The last save failed, and the stored settings no longer pass their check.',
  formTitle: 'Change the settings',
  formIntro: 'Saved values take effect at the next boot. Nothing changes on the running interface.',
  review: 'Review and save',
  fieldErrors: {
    not_ipv4: 'Enter an address as four numbers from 0 to 255, such as 192.0.2.20.',
    not_unicast: 'This is not a usable device address on this subnet.',
    prefix_not_usable: 'Enter a prefix length from 1 to 30.',
    gateway_outside_subnet: 'The gateway must be inside the subnet of the address.',
    gateway_not_usable: 'The gateway must be another device on this subnet. Leave it empty for no gateway.',
  },
  fieldUnknown: 'The device refused this value.',
  confirmTitle: 'Before you save',
  confirmSummary: (address: string, prefix: number, gateway: string) =>
    `New values: address ${address}/${prefix}, gateway ${gateway}.`,
  confirmNext: (address: string) =>
    `At the next boot the device answers at ${address}, not at the address you are using now. Connect to the new address after that. A certificate that names the old address may also stop matching.`,
  confirmAnyReset:
    'Saved values take effect at any later reset, including a power cut, and not only after a restart that you ask for.',
  confirmRecovery:
    'If the new address cannot be reached, use the recovery command on the serial console to return to the default values. A device with no serial console needs its own recovery path.',
  confirm: 'Save the settings',
  cancel: 'Cancel',
  saving: 'Saving the settings…',
  saved_: 'The settings are saved. They take effect at the next boot.',
  saveFailed: 'The save failed. The stored settings no longer pass their check, so the next boot uses the default values. Try saving again.',
  saveUnconfirmed: 'The device accepted the settings, but the save was not confirmed in time. Check the saved values below.',
  saveUnreadable: 'The device accepted the settings. This account may not read network settings, so it cannot see whether the save succeeded.',
  readDenied: 'This account may not read the network settings. If it may change them, the form below still works, but it cannot show the result.',
  notSupported:
    'This device cannot save network settings, because it has no Configuration provider. Nothing was changed. The running address stays in use, and only new firmware can change it.',
  saveDenied: 'This account may not change the network settings.',
};

export const REBOOT = {
  title: 'Restart the device',
  navigation: 'Restart',
  intro: 'A restart ends every session on the device, including this one. The application on the device can refuse it.',
  savedAddress: (address: string) => `Saved network settings take effect at this restart, so the device will answer at ${address}.`,
  start: 'Restart the device…',
  confirmTitle: 'Restart the device now?',
  confirmBody: 'The device stops answering while it restarts. The application on the device may be running a machine, and it decides whether to allow this.',
  confirm: 'Restart now',
  cancel: 'Cancel',
  denied: 'This account may not restart the device.',
  refused: (reason: string) => `The device refused the restart: ${reason}`,
  failed: 'The restart failed. The device is still running, and its session is still valid.',
  previousFailed: 'The last restart request failed. The device is still running.',
  previousRefused: (reason: string) => `The last restart request was refused: ${reason}`,
  waitingRunning: 'Waiting for the device to restart. It is still answering, which alone does not mean anything has happened yet.',
  waitingAway: 'The device is not answering, which is expected while it restarts.',
  overdue: 'This is taking longer than expected.',
  notConfirmedRunning:
    'The restart was not confirmed. The device is still answering and this session is still valid, so it probably has not restarted. Check the device, then try again.',
  notConfirmedAway:
    'The restart was not confirmed. The device has not answered for longer than expected. It may be at another address if saved settings applied, or it may not have come back. Check its power and its serial console.',
  keepWaiting: 'Keep waiting',
  back: 'Back to the restart page',
};

/** What each failure comes to, by the code of the device's error or a stand-in. */
export const ERRORS: Record<string, string> = {
  network: 'The device did not answer. Check the connection and try again.',
  busy: 'The device is busy. Try again in a moment.',
  not_ready: 'A service on the device is not ready. Try again shortly.',
  unauthorized: 'Your session has ended. Sign in again to continue.',
  permission_denied: 'This account does not have permission to do this.',
  csrf_rejected: 'The device refused this request, because its security token did not match. Reload the page and try again.',
  origin_rejected: 'The device refused this request, because it did not come from the address of the device.',
  bad_request: 'The device could not read this request.',
  invalid_field: 'The device refused one of the values.',
  not_found: 'The device has no such item.',
  payload_too_large: 'This request is too large for the device.',
  reboot_refused: 'The device refused the restart.',
  retry_later: 'Too many attempts. Try again later.',
  not_supported: 'This device does not support this operation.',
  no_route: 'This device does not offer this feature.',
  method_not_allowed: 'This device does not offer this operation.',
  unexpected: 'The device gave an answer that this interface does not understand.',
};

export function lookup(table: Record<string, string>, key: string): string | undefined {
  return Object.hasOwn(table, key) ? table[key] : undefined;
}

/** The server's own plain `404` and `405` have no JSON body, so they come as `unexpected` with their status. */
export function errorText(failure: Failure): string {
  if (failure.code === 'unexpected' && failure.status === 404) {
    return ERRORS['no_route'] as string;
  }
  if (failure.code === 'unexpected' && failure.status === 405) {
    return ERRORS['method_not_allowed'] as string;
  }
  return lookup(ERRORS, failure.code) ?? (ERRORS['unexpected'] as string);
}
