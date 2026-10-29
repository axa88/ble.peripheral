#pragma once
namespace GattRebuild {
enum class Outcome { PreconditionsFailed, MutationFailed, ResetFailed,
                     RegistrationFailed, VerificationFailed, Applied };
template <typename Adapter>
Outcome Run(Adapter& adapter) {
    if (!adapter.idle()) return Outcome::PreconditionsFailed;
    if (!adapter.mutate()) return Outcome::MutationFailed;
    if (!adapter.reset()) return Outcome::ResetFailed;
    adapter.stop();
    if (!adapter.start()) return Outcome::RegistrationFailed;
    if (!adapter.verify()) return Outcome::VerificationFailed;
    return Outcome::Applied;
}
} // namespace GattRebuild
