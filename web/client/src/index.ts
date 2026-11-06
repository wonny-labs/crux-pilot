// The Crux management API client. No framework, no dependency, and no secret held.
export { createClient, defaultClientTiming } from './client.ts';
export type {
  AttemptOptions,
  AttemptOutcome,
  AttemptProgress,
  CallOptions,
  ClientOptions,
  ClientTiming,
  ConfigInput,
  ConfigSaveOutcome,
  CruxClient,
  QuietOptions,
  RebootOutcome,
  RebootUpdate,
  RejectionCode,
  SaveOutcome,
  Wait,
  WatchRebootOptions,
} from './client.ts';
export type { ApiError, ApiErrorKind, Result } from './errors.ts';
export { parseConfigInput } from './config-input.ts';
export type { ConfigInputProblem, ConfigInputResult } from './config-input.ts';
export { checkIpv4Settings, parseIpv4 } from './ipv4.ts';
export type { Ipv4Input, Ipv4Problem, Ipv4ProblemCode } from './ipv4.ts';
export type {
  ConfigItem,
  ConfigList,
  ConfigSaveFailed,
  ConfigSaveState,
  ConfigValue,
  CurrentUser,
  ErrorCode,
  FieldError,
  Ipv4Settings,
  NetworkState,
  RebootAccepted,
  RebootState,
  SaveQueued,
  StatusItem,
  StatusReport,
} from './api-types.generated.ts';
export { operations } from './api-types.generated.ts';
