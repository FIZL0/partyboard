#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// N111: one UIImpactFeedbackGenerator tap, medium if heavy else light. Callable from any thread.
void PartyBoard_iOS_ImpactHaptic(int heavy);

#ifdef __cplusplus
}
#endif
