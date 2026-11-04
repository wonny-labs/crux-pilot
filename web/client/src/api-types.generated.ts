// Generated from web/api/openapi.yaml by web/tools/gen-types.ts. Do not edit.
// Run `npm run gen:types` after a change to the specification.

/** Stable error codes. A new code is a change to this file. `storage_failed` appears in an attempt result only, and no HTTP error status carries it. */
export type ErrorCode = 'bad_request' | 'invalid_field' | 'unauthorized' | 'permission_denied' | 'csrf_rejected' | 'origin_rejected' | 'not_found' | 'payload_too_large' | 'reboot_refused' | 'retry_later' | 'not_supported' | 'busy' | 'not_ready' | 'storage_failed';

export interface FieldError {
  name: string;
  code: 'not_unicast' | 'prefix_not_usable' | 'gateway_outside_subnet' | 'incorrect' | 'rule_not_met';
}

/** The JSON error form, which every response from the API's own code uses for an error. */
export interface Error {
  code: ErrorCode;
  fields?: FieldError[];
  reason?: RefusalReason;
}

/** The text that the application's reboot policy gave, with `reboot_refused` or `refused` only. At most 63 bytes of UTF-8, the bound of a status text value, and it follows the same character rule. The schema counts characters, so the server enforces the byte bound. */
export type RefusalReason = string;

export interface LoginRequest {
  user: string;
  password: string;
}

export interface AttemptAccepted {
  id: string;
}

/** An attempt's result. A login's `rejected` has no `code`. A password change's `rejected` always has one, and `fields` appears only with `invalid_field`. */
export type SessionAttempt = AttemptOpenOrDone | AttemptRejectedWithoutReason | AttemptRejectedFields | AttemptRejectedCode;

export interface AttemptOpenOrDone {
  state: 'pending' | 'success';
}

/** A rejected login, with no detail. */
export interface AttemptRejectedWithoutReason {
  state: 'rejected';
}

/** A rejected password change, with one entry for each rejected field. */
export interface AttemptRejectedFields {
  state: 'rejected';
  code: ErrorCode & 'invalid_field';
  fields: FieldError[];
}

/** A password change that ended for another reason. `bad_request`: the account service refused an argument (`INVALID_ARGUMENT`), and nothing changed. `storage_failed`: the save failed, the running password is unchanged, and a later boot may use either. `not_ready`: the account service could not run it. `unauthorized`: the session ended while the attempt ran. */
export interface AttemptRejectedCode {
  state: 'rejected';
  code: ErrorCode & ('bad_request' | 'storage_failed' | 'not_ready' | 'unauthorized');
}

export interface CurrentUser {
  user: string;
  role: string;
  must_change_password: boolean;
}

export interface PasswordChangeRequest {
  current_password: string;
  new_password: string;
}

export interface StatusReport {
  items: StatusItem[];
}

/** One status item. An item whose source failed has no `type` or `value`. */
export type StatusItem = StatusItemBool | StatusItemU32 | StatusItemU64 | StatusItemText | StatusItemFailed;

/** Project item names are 1 to 31 characters. Built-in names can be longer. */
export type StatusItemName = string;

export type StatusItemOk = 'ok';

export interface StatusItemBool {
  name: StatusItemName;
  status: StatusItemOk;
  type: 'bool';
  value: boolean;
}

export interface StatusItemU32 {
  name: StatusItemName;
  status: StatusItemOk;
  type: 'u32';
  value: number;
}

export interface StatusItemU64 {
  name: StatusItemName;
  status: StatusItemOk;
  type: 'u64';
  value: string;
}

export interface StatusItemText {
  name: StatusItemName;
  status: StatusItemOk;
  type: 'text';
  value: string;
}

export interface StatusItemFailed {
  name: StatusItemName;
  status: 'invalid_argument' | 'out_of_range' | 'not_ready' | 'no_data' | 'io' | 'busy' | 'timeout' | 'no_resources' | 'not_supported' | 'invalid_state';
}

/** Static IPv4 settings. The server also checks that the address is unicast, that the prefix leaves usable hosts and that a gateway lies inside the subnet, and it reports each failure as a field error. */
export interface Ipv4Settings {
  address: string;
  prefix_length: number;
  gateway?: string;
}

export interface NetworkState {
  running?: Ipv4Settings;
  saved?: Ipv4Settings;
  source: 'saved' | 'default';
  reboot_required: boolean;
  save?: SaveState;
}

/** Present while a save is pending, and after a failed save until the next accepted one. */
export interface SaveState {
  state: 'pending' | 'failed';
}

export interface SaveQueued {
  state: 'pending';
}

/** The state of the latest reboot request. `refused` always has its `reason`. */
export type RebootState = RebootStateWithoutReason | RebootStateRefused;

export interface RebootStateWithoutReason {
  state: 'none' | 'pending' | 'failed';
}

export interface RebootStateRefused {
  state: 'refused';
  reason: RefusalReason;
}

export interface RebootAccepted {
  state: 'pending';
}

export const operations = {
  createSession: { method: 'POST', path: '/api/v1/session' },
  deleteSession: { method: 'DELETE', path: '/api/v1/session' },
  getSessionAttempt: { method: 'GET', path: '/api/v1/session/attempt/{id}' },
  getCurrentUser: { method: 'GET', path: '/api/v1/session/me' },
  changeOwnPassword: { method: 'POST', path: '/api/v1/session/me/password' },
  getStatus: { method: 'GET', path: '/api/v1/status' },
  getNetwork: { method: 'GET', path: '/api/v1/network' },
  putNetwork: { method: 'PUT', path: '/api/v1/network' },
  getRebootState: { method: 'GET', path: '/api/v1/system/reboot' },
  requestReboot: { method: 'POST', path: '/api/v1/system/reboot' },
} as const;

export type OperationId = keyof typeof operations;
