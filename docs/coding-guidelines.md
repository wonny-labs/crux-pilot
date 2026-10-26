# Coding and Public API Documentation Guidelines

## Purpose and scope

These guidelines apply to new or materially changed Crux-owned C17 code and public API declarations. They help contributors and AI coding agents produce consistent, reviewable code without changing the existing architecture or supported contracts. [CONTRIBUTING.md](../CONTRIBUTING.md) remains the entry point for contribution and review rules. Adopted [capability contracts](architecture.md#current-capability-integration) define supported behaviour.

Apply these rules incrementally. Do not reformat unrelated code, rename existing interfaces or convert every public header solely for consistency. External Zephyr, vendor and third-party sources follow their own requirements.

## Coding conventions

- **Language and portability:** Shared components use ISO C17. Public C declarations remain usable by the documented C++17 consumers through `extern "C"` where applicable. Do not leak OS, SoC, board or provider-private types into portable public headers. Qualified standard APIs are deliberate exceptions to Crux-owned interfaces, not general native access.
- **Formatting:** [.clang-format](../.clang-format) defines the owned-source style: LLVM-based formatting with Linux brace placement, 4-space indentation and a 100-column limit. The adopted change is limited to brace placement; keep all other settings from the configuration. Check with [`scripts/check-format.sh`](../scripts/check-format.sh) and the pinned formatter. Follow nearby code where a choice is not specified.
- **Clarity:** Use descriptive names and explicit units. Include braces on new or modified control-flow bodies, even for a single statement. Check range conversions and arithmetic when overflow would matter. Do not reformat unrelated code.
- **Naming:** Use `crux_` for public C functions, types and exported variables, and `CRUX_` for macros, enum constants and header guards, as defined in [CONTRIBUTING.md](../CONTRIBUTING.md#making-changes). Keep component-owned `<crux/...>` headers. Do not rename public targets, native providers or Zephyr identifiers for style alone.
- **Resources and errors:** Handle allocation, lifetime, cleanup after partial failure and concurrent access explicitly. Preserve adopted component-specific result types and named errors rather than forcing all APIs into one error enum.
- **Platform boundaries:** Portable consumers depend on public Crux contracts or explicitly qualified standard APIs. OS-specific ports and platform composition own native requirements. Do not introduce a universal HAL/OSAL or service registry without real consumer evidence.
- **Execution and safety:** Specify supported thread or ISR context, blocking, timeout units and resource limits. Do not promise real-time bounds, ISR safety, persistence or cross-OS equivalence without contract and evidence.

The existing [.clang-tidy](../.clang-tidy) selection, compiler warnings and tests are the configured automated checks. They are not a claim of full MISRA C, CERT C, security or functional-safety compliance. New mandatory checks and toolchain changes require separate justification.

## Public API documentation

For new or materially changed public declarations, use Doxygen-compatible `/** ... */` comments in the owning header. Existing untouched headers can be documented incrementally when relevant. A generated API site, Doxygen groups and a full-header conversion are not required by the current Pilot.

Document the following when relevant to a caller:

- **Purpose and values:** Behaviour, accepted inputs, ranges, units, clock or time base, special values and prerequisites.
- **Outputs and errors:** Success and failure results, unchanged outputs, partial success, failure effects and retry or recovery rules.
- **Ownership and lifetime:** Ownership and validity of handles, buffers, callback contexts and stored references, including failure or close.
- **Execution context:** Blocking, timeouts, concurrency, re-entrancy, caller-side synchronisation and ISR limits.
- **Platform support:** Restrictions that affect the public contract, with detailed configurations and test evidence linked from the component contract.

Use an imperative one-sentence `@brief` or a clear first sentence. Document parameters in declaration order with `@param`. Use `@param[out]` and `@param[in,out]` for writable parameters where appropriate. Use `@retval` for discrete, actually possible status values and `@return` for general or provider-dependent results. Do not list unrelated errors or invent guarantees. Reserve `@note` and `@warning` for information that changes correct use.

Explain public types, enum values, members and macros when their meaning, values or ownership are non-obvious. Use brief `/** ... */` comments or `/**< ... */` trailing comments. Avoid repeating the identifier or C type in prose. Keep internal design rationale and implementation details out of public API descriptions.

Use the owning component's current public headers and adopted contract as examples of appropriate detail. Public headers should be quick references for correct use, not copies of complete design or validation reports.

## Review and validation

Review new or changed API declarations, documentation, implementation and focused tests together. Compare advertised outputs, errors, ownership, context and supported environments with actual behaviour. Record incompatible changes explicitly instead of treating documentation edits as API approval.

Use existing format, dependency, build and test checks proportional to the change. `scripts/analyze-host.sh` needs its documented configured Host build and covers only its selected source files. Documentation-only changes need link and accuracy review, not unrelated hardware tests.

## Upstream guidance

These references inform selected practices. They do not override Crux's established formatting, public contracts or review rules.

- [Zephyr C Code and General Style Guidelines](https://docs.zephyrproject.org/latest/contribute/style/code.html)
- [Zephyr Doxygen Style Guidelines](https://docs.zephyrproject.org/latest/contribute/style/doxygen.html)
- [Zephyr API Design Guidelines](https://docs.zephyrproject.org/latest/develop/api/design_guidelines.html)
- [Pigweed C++ style](https://pigweed.dev/style/cpp.html)
