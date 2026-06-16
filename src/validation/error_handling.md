# Error Handling Contract

Validation code classifies failure before choosing a C++ mechanism.

## Consensus Invalidity

An invalid block, header, transaction, or script is an ordinary validation
outcome. Report it as a value result, such as `Consensus::BlockSpendResult`, and
translate it to `BlockValidationState::Invalid` at Core validation boundaries.
Do not throw for candidate rejection.

Examples:

- missing or spent transaction inputs
- invalid header proof or timestamp
- script verification failure
- mutated block data

## Validation Runtime Failure

A validator/runtime failure means the candidate was not classified because the
validation machinery could not complete its work. Report it as a typed runtime
error and translate it to `BlockValidationState::Error`.

Examples:

- stale IBD segment or stale commit target
- resource limit while preparing validation work
- disk or backend failure
- cancellation
- script executor or thread-pool failure

## Programming Error

A violated internal precondition, broken invariant, impossible state, or null
argument documented as non-null is a programming error. Use assertions or
deliberate termination, not peer-facing invalidity.

## Kernel API Boundary

The public `bitcoinkernel` C API must not leak C++ exceptions. Exported
functions translate recoverable C++ exceptions to their documented C result:
`nullptr`, `-1`, or a status out-parameter. Callback functions are foreign
boundaries and must not throw.

Fallible C API mutators return a status directly. Do not add `void` mutators
for operations that allocate, copy callbacks, touch filesystem-backed options,
or otherwise can fail before the dependent kernel object is created.

## Fatal Failure

If continuing would violate process or chain-state invariants, report once at
the owning boundary and terminate or return a system error that directs callers
to tear down the affected kernel objects.
