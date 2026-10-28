#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs {
void setIpuInitTables(uint32_t iqBase, uint32_t vqBase,
                      uint32_t setD4ChcrEntry); // per-game libipu table addresses (0 = no helper)
void sceIpuInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);
void sceIpuRestartDMA(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);
void sceIpuStopDMA(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);
void sceIpuSync(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);
} // namespace ps2_stubs
