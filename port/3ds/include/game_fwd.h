#ifndef _GAME_FWD_H_
#define _GAME_FWD_H_

// Forward declarations of types and externs used across shim files
// and the original game code. Keep this header light to avoid
// pulling in the entire common_structs.h chain.

#include "pm_types.h"

// Original game functions declared as externs
extern void boot_main(void* data);
extern void step_game_loop(void);
extern void gfx_draw_frame(void);
extern void gfx_task_background(void);
extern void gfx_init_state(void);
extern void create_audio_system(void);
extern void load_engine_data(void);
extern void load_obfuscation_shims(void);
extern void is_debug_init(void);
extern void crash_screen_init(void);
extern void crash_screen_set_draw_info(u16* framebuffer, s32 width, s32 height);
extern void render_frame(s32 flag);
extern void spr_render_init(void);
extern void render_messages(void);
extern void render_window_root(void);
extern void render_curtains(void);
extern void render_workers_backUI(void);
extern void render_workers_frontUI(void);
extern void render_hud_elements_backUI(void);
extern void render_hud_elements_frontUI(void);
extern void render_screen_overlay_backUI(void);
extern void render_screen_overlay_frontUI(void);
extern void render_game_mode_backUI(void);
extern void render_game_mode_frontUI(void);
extern void render_effects_UI(void);
extern void player_render_interact_prompts(void);
extern void func_802C3EE4(void);
extern void mdl_reset_transform_flags(void);
extern s32 npc_iter_no_op(void);
extern void update_workers(void);
extern void update_triggers(void);
extern void update_scripts(void);
extern void update_messages(void);
extern void update_hud_elements(void);
extern void step_game_mode(void);
extern void update_entities(void);
extern void func_80138198(void);
extern void bgm_update_music_control(void);
extern void update_ambient_sounds(void);
extern void sfx_update_env_sound_params(void);
extern void update_windows(void);
extern void update_curtains(void);
extern void poll_rumble(void);
extern void disable_player_input(void);
extern void set_game_mode(s16 mode);
extern s32 resume_all_group(s32 groupFlags);
extern void suspend_all_group(s32 groupFlags);
extern void update_max_rumble_duration(void);
extern s32 rand_int(s32 max);
extern void clear_input(void);
extern void general_heap_create(void);
extern void clear_render_tasks(void);
extern void clear_worker_list(void);
extern void clear_script_list(void);
extern void create_cameras(void);
extern void clear_player_status(void);
extern void spr_init_sprites(s32 type);
extern void clear_entity_models(void);
extern void clear_animator_list(void);
extern void clear_model_data(void);
extern void clear_sprite_shading_data(void);
extern void reset_background_settings(void);
extern void clear_character_set(void);
extern void clear_printers(void);
extern void clear_game_mode(void);
extern void clear_npcs(void);
extern void hud_element_clear_cache(void);
extern void clear_trigger_data(void);
extern void clear_entity_data(s32 flag);
extern void clear_player_data(void);
extern void init_encounter_status(void);
extern void clear_screen_overlays(void);
extern void clear_effect_data(void);
extern void clear_saved_variables(void);
extern void clear_item_entity_data(void);
extern void bgm_reset_sequence_players(void);
extern void reset_ambient_sounds(void);
extern void sfx_clear_sounds(void);
extern void initialize_curtains(void);
extern void fio_init_flash(void);
extern s32 _render_transition_stencil(s32 overlayType, s16 alpha, void* arg2);

#endif
