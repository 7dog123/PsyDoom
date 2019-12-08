#pragma once

void psxspu_init_reverb() noexcept;
void psxspu_set_reverb_depth() noexcept;
void psxspu_init() noexcept;
void psxspu_update_master_vol() noexcept;
void psxspu_update_master_vol_mode() noexcept;
void psxspu_setcdmixon() noexcept;
void psxspu_setcdmixoff() noexcept;
void psxspu_fadeengine() noexcept;
void psxspu_set_cd_vol() noexcept;
void psxspu_get_cd_vol() noexcept;
void psxspu_start_cd_fade() noexcept;
void psxspu_stop_cd_fade() noexcept;
void psxspu_get_cd_fade_status() noexcept;
void psxspu_set_master_vol() noexcept;
void psxspu_get_master_vol() noexcept;
void psxspu_start_master_fade() noexcept;
void psxspu_stop_master_fade() noexcept;
void psxspu_get_master_fade_status() noexcept;
