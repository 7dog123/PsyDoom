#include "le_main.h"

#include "Doom/Base/i_crossfade.h"
#include "Doom/Base/i_main.h"
#include "Doom/Base/s_sound.h"
#include "Doom/Base/sounds.h"
#include "Doom/d_main.h"
#include "Doom/Game/p_tick.h"
#include "Doom/Renderer/r_data.h"
#include "m_main.h"
#include "PsxVm/PsxVm.h"
#include "ti_main.h"

// Texture for the legals screen text
static const VmPtr<texture_t> gTex_LEGALS(0x80097BD0);

//------------------------------------------------------------------------------------------------------------------------------------------
// Startup/init logic for the 'legals' screen
//------------------------------------------------------------------------------------------------------------------------------------------
void START_Legals() noexcept {
    I_PurgeTexCache();    
    I_LoadAndCacheTexLump(*gTex_LEGALS, "LEGALS", 0);

    a0 = 0;
    a1 = sfx_sgcock;
    S_StartSound();

    *gTitleScreenSpriteY = SCREEN_H;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Shutdown logic for the 'legals' screen
//------------------------------------------------------------------------------------------------------------------------------------------
void STOP_Legals() noexcept {
    a0 = 0;
    a1 = sfx_barexp;
    S_StartSound();

    I_CrossFadeFrameBuffers();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Update logic for the 'legals' screen
//------------------------------------------------------------------------------------------------------------------------------------------
gameaction_t TIC_Legals() noexcept {
    // Scroll the legal text, otherwise check for timeout
    if (*gTitleScreenSpriteY > 0) {
        *gTitleScreenSpriteY = *gTitleScreenSpriteY - 1;
    
        if (*gTitleScreenSpriteY == 0) {
            *gMenuTimeoutStartTicCon = *gTicCon;
        }
    } else {
        // Must hold the legals text for a small amount of time before allowing skip (via a button press) or timeout
        const int32_t waitTicsElapsed = *gTicCon - *gMenuTimeoutStartTicCon;
        
        if (waitTicsElapsed > 120) {
            if (waitTicsElapsed >= 180) 
                return ga_timeout;
            
            if (gTicButtons[0] != 0)
                return ga_exit;
        }
    }

    return ga_nothing;
}

void _thunk_TIC_Legals() noexcept {
    v0 = TIC_Legals();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Does drawing for the 'legals' screen - very simple, just a single sprite
//------------------------------------------------------------------------------------------------------------------------------------------
void DRAW_Legals() noexcept {
    I_IncDrawnFrameCount();    
    I_CacheAndDrawSprite(*gTex_LEGALS, 0, (int16_t) *gTitleScreenSpriteY, gPaletteClutIds[UIPAL]);

    I_SubmitGpuCmds();
    I_DrawPresent();
}
