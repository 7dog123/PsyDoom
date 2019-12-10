#pragma once

#include <cstdint>

void LIBETC_ResetCallback() noexcept;
void LIBETC_InterruptCallback() noexcept;
void LIBETC_DMACallback() noexcept;
void LIBETC_VSyncCallbacks() noexcept;
void LIBETC_StopCallback() noexcept;
void LIBETC_CheckCallback() noexcept;
void LIBETC_GetIntrMask() noexcept;
void LIBETC_SetIntrMask() noexcept;
void LIBETC_INTR_startIntr() noexcept;
void LIBETC_INTR_trapIntr() noexcept;
void LIBETC_INTR_setIntr() noexcept;
void LIBETC_INTR_stopIntr() noexcept;
void LIBETC_INTR_memclr() noexcept;
void LIBETC_startIntrVSync() noexcept;
void LIBETC_INTR_stopIntr_UNKNOWN_Helper2() noexcept;
void LIBETC_INTR_VB_trapIntrVSync() noexcept;
void LIBETC_INTR_VB_setIntrVSync() noexcept;
void LIBETC_INTR_VB_memclr() noexcept;
void LIBETC_startIntrDMA() noexcept;
void LIBETC_INTR_stopIntr_UNKNOWN_Helper1() noexcept;
void LIBETC_INTR_DMA_trapIntrDMA() noexcept;
void LIBETC_INTR_DMA_setIntrDMA() noexcept;
void LIBETC_INTR_DMA_memclr() noexcept;
void LIBETC_VSync() noexcept;
void LIBETC_v_wait(const int32_t targetVCount, const uint16_t timeout) noexcept;
void _thunk_LIBETC_v_wait() noexcept;
void LIBETC_SetVideoMode() noexcept;
void LIBETC_GetVideoMode() noexcept;
