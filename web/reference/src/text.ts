// All user text, in English, in one table ready for translation. A project passes overrides through
// the `text` option of mountReferenceInterface and changes no other file.
import type { ApiErrorKind } from '../../client/src/index.ts';

function seconds(count: number): string {
  return count === 1 ? '1 second' : `${count} seconds`;
}

export const englishText = {
  product: 'Controller',
  loading: 'Connecting to the device…',
  notFound: {
    title: 'Page not found',
    body: 'This address does not match a page of this interface.',
    home: 'Go to home',
  },
  shell: {
    navigation: 'Main',
    signedInAs: (user: string, role: string) => `Signed in as ${user} (${role})`,
    signOut: 'Sign out',
    signOutFailed: 'Could not sign out, and the session is still active.',
    tryAgain: 'Try again',
    timeLeft: (wait: number) => `Time left: ${seconds(wait)}.`,
  },
  signIn: {
    title: 'Sign in',
    user: 'User name',
    password: 'Password',
    submit: 'Sign in',
    submitting: 'Sending your sign-in to the device…',
    verifying: 'Checking your password. This can take a few seconds.',
    waiting: 'The device is busy. Trying again shortly.',
    needUser: 'Enter your user name.',
    needPassword: 'Enter your password.',
    rejected:
      'Sign-in failed. Check the user name and password. A device with no account yet must first be set up on its serial console.',
    delayed: (wait: number) => `Too many attempts. Try again in ${seconds(wait)}.`,
    busy: (wait: number) => `The device is busy. Try again in ${seconds(wait)}.`,
    lost: 'The device lost the result of this sign-in. Sign in again.',
    timedOut: 'The device did not finish checking in time. Try again.',
    sessionEnded: 'Your session has ended. Sign in again to continue.',
    restarted: 'The device has restarted. Sign in again to continue.',
    signedOut: 'You have signed out.',
    sessionNotKept:
      'The device accepted your sign-in, but the session was not there on the next request. The browser may have refused the cookie, so load this page over HTTPS from the address of the device and allow cookies for it. The device may also have ended the session. Sign in again.',
  },
  password: {
    title: 'Change your password',
    intro: 'This account must change its password before it can do anything else.',
    current: 'Current password',
    next: 'New password',
    repeat: 'Repeat the new password',
    rule: 'The device checks the new password against its password rule. The reference rule is 12 to 128 bytes.',
    submit: 'Change password',
    submitting: 'Sending the change to the device…',
    verifying: 'Checking and saving the password. This can take a few seconds.',
    waiting: 'The device is busy. Trying again shortly.',
    needCurrent: 'Enter your current password.',
    needNext: 'Enter a new password.',
    mismatch: 'The new passwords do not match.',
    incorrect: 'The current password is not correct.',
    incorrectAfterLost:
      'The current password was not accepted. An earlier attempt may have taken effect, so the new password may already be the current one. Enter it as the current password, or check with an administrator.',
    ruleNotMet: "The new password does not meet the device's password rule.",
    fieldUnknown: 'The device refused this value.',
    storageFailed:
      'The new password could not be saved. The running password is unchanged, and a later boot may use either password. Try again.',
    badRequest: 'The device refused this change. Check the values and try again.',
    notReady: 'The account service on the device is not ready. Try again shortly.',
    delayed: (wait: number) => `Too many attempts. Try again in ${seconds(wait)}.`,
    busy: (wait: number) => `The device is busy. Try again in ${seconds(wait)}.`,
    lost: 'The result was lost, so this change may or may not have taken effect. Enter the passwords again.',
    timedOut: 'The device did not finish in time. Check whether the change took effect, then try again.',
  },
  home: {
    title: 'Home',
    navigation: 'Home',
    nothing: 'There is nothing to show to this account.',
    unavailable: 'Not available',
    refresh: 'Refresh now',
    updatedAt: (time: string) => `Updated at ${time}`,
    stale: (time: string) => `Showing values from ${time}.`,
    otherItems: 'Other items',
    yes: 'Yes',
    no: 'No',
    up: 'Up',
    down: 'Down',
    sections: {
      versions: 'Versions',
      network: 'Network',
      reset: 'Last reset',
      time: 'Time',
    },
    items: {
      'version.platform': 'Platform version',
      'version.project': 'Project version',
      'net.link': 'Link',
      'net.admin_up': 'Interface enabled',
      'net.ipv4': 'IPv4 address',
      'reset.causes': 'Reset causes',
      'reset.supported': 'Reset causes reported',
      'system.uptime_ms': 'Time since boot',
      'system.clock': 'Clock',
      'system.watchdog_state': 'Watchdog',
    } as Record<string, string>,
    uptime: (days: bigint, hours: bigint, minutes: bigint) =>
      `${days > 0n ? `${days} d ` : ''}${hours} h ${minutes} min`,
    itemStatus: {
      invalid_argument: 'Not available (invalid request)',
      out_of_range: 'Not available (out of range)',
      not_ready: 'Not available (not ready)',
      no_data: 'Not available (no data)',
      io: 'Not available (read failed)',
      busy: 'Not available (busy)',
      timeout: 'Not available (timed out)',
      no_resources: 'Not available (no resources)',
      not_supported: 'Not available (not supported on this device)',
      invalid_state: 'Not available (not in a state to report)',
    } as Record<string, string>,
  },
  network: {
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
    } as Record<string, string>,
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
  },
  reboot: {
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
  },
  errors: {
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
    no_route: 'This device does not offer this feature.',
    method_not_allowed: 'This device does not offer this operation.',
    payload_too_large: 'This request is too large for the device.',
    reboot_refused: 'The device refused the restart.',
    retry_later: 'Too many attempts. Try again later.',
    not_supported: 'This device does not support this operation.',
    aborted: 'The request was stopped.',
    unexpected: 'The device gave an answer that this interface does not understand.',
  } satisfies Record<ApiErrorKind, string>,
};

export type Text = typeof englishText;

type DeepPartial<T> = {
  [K in keyof T]?: T[K] extends (...args: never[]) => unknown ? T[K] : T[K] extends object ? DeepPartial<T[K]> : T[K];
};

export type TextOverrides = DeepPartial<Text>;

/** A table entry by a name from the device, which must not reach built-in properties such as constructor. */
export function lookup(table: Record<string, string>, key: string): string | undefined {
  return Object.hasOwn(table, key) ? table[key] : undefined;
}

function isTable(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

/** The base table with the overrides laid over it, leaving out nothing that they do not name. */
export function mergeText(base: Text, overrides: TextOverrides = {}): Text {
  const merge = (a: Record<string, unknown>, b: Record<string, unknown>): Record<string, unknown> => {
    const result: Record<string, unknown> = { ...a };
    for (const [key, value] of Object.entries(b)) {
      const current = result[key];
      result[key] = isTable(current) && isTable(value) ? merge(current, value) : value;
    }
    return result;
  };
  return merge(base as unknown as Record<string, unknown>, overrides as Record<string, unknown>) as unknown as Text;
}
