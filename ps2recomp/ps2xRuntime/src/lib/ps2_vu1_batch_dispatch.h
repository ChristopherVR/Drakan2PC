#pragma once
#include "vu/ps2_vu1_step.h"
namespace vu1batch {
bool tracking();
bool dispatch(VU1Interpreter&, Vu1Step::Ctx&);
} // namespace vu1batch
