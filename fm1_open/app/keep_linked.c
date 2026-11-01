#ifndef FM1_HOST_TEST
/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* sdk/Build.ps1 requires engine code (the sequencer, DX7, effects, master gain, ...) to be linked into internal SRAM. The stock application referenced
 * them (sequencer, panel, project code); the SF2 player does not, and the linker drops what nothing references, so
 * this table references every global the library defines, so everything the stock application could reach stays. Never read.
 * Names only, no headers. */
extern void __errno(void);
extern void _start(void);
extern void abort(void);
extern void audio_app_bluetooth_open(void);
extern void bass303_param(void);
extern void bt_get_local_name(void);
extern void bt_get_mac_addr(void);
extern void calloc(void);
extern void clock_port(void);
extern void clock_timer1(void);
extern void connectivity_wl82_bluetooth_start(void);
extern void drum808_nparams(void);
extern void drum808_param(void);
extern void ecvtbuf(void);
extern void fcvtbuf(void);
extern void fm1_app_midi_received(void);
extern void fm1_app_sysex(void);
extern void fm1_ble_connect(void);
extern void fm1_ble_disconnect(void);
extern void fm1_ble_forget_bonds(void);
extern void fm1_ble_get_device(void);
extern void fm1_ble_get_status(void);
extern void fm1_ble_scan(void);
extern void fm1_ble_set_role(void);
extern void fm1_bluetooth_audio_read(void);
extern void fm1_bluetooth_configure(void);
extern void fm1_bluetooth_get_status(void);
extern void fm1_control_request(void);
extern void fm1_device_adc(void);
extern void fm1_device_adc_register(void);
extern void fm1_device_clock(void);
extern void fm1_device_delay(void);
extern void fm1_device_fence(void);
extern void fm1_device_irq_attach(void);
extern void fm1_device_irq_restore(void);
extern void fm1_device_irq_save(void);
extern void fm1_device_recovery(void);
extern void fm1_device_ticks(void);
extern void fm1_device_usb_connected(void);
extern void fm1_engine_output_level(void);
extern void fm1_engine_pause(void);
extern void fm1_engine_render(void);
extern void fm1_engine_resume(void);
extern void fm1_engine_stats_read(void);
extern void fm1_engine_stop(void);
extern void fm1_flash_erase(void);
extern void fm1_flash_invalidate(void);
extern void fm1_flash_map(void);
extern void fm1_flash_read(void);
extern void fm1_flash_size(void);
extern void fm1_flash_write(void);
extern void fm1_io_trs_midi_enabled(void);
extern void fm1_midi_input_ports(void);
extern void fm1_midi_input_source(void);
extern void fm1_midi_output_ports(void);
extern void fm1_midi_post(void);
extern void fm1_midi_ready_ports(void);
extern void fm1_midi_send(void);
extern void fm1_midi_send_port(void);
extern void fm1_millis(void);
extern void fm1_r_f4157fee5abb(void);
extern void fm1_sea_decode(void);
extern void fm1_sea_decode_groups(void);
extern void fm1_usb_audio_read(void);
extern void fm1_usb_audio_write(void);
extern void free(void);
extern void groove_adsr_edit(void);
extern void groove_adsr_enable(void);
extern void groove_arp_edit(void);
extern void groove_arp_enabled(void);
extern void groove_arp_set(void);
extern void groove_arp_setting(void);
extern void groove_arrangement_capture(void);
extern void groove_arrangement_tuning_apply(void);
extern void groove_arrangement_tuning_capture(void);
extern void groove_audio_stats(void);
extern void groove_clock_mode(void);
extern void groove_clock_set(void);
extern void groove_control_edit(void);
extern void groove_delay_ticks(void);
extern void groove_drum_index(void);
extern void groove_drum_note(void);
extern void groove_drum_steps(void);
extern void groove_edit_checkpoint(void);
extern void groove_edit_clear(void);
extern void groove_edit_clipboard_count(void);
extern void groove_edit_copy(void);
extern void groove_edit_cursor(void);
extern void groove_edit_error(void);
extern void groove_edit_forget_history(void);
extern void groove_edit_init(void);
extern void groove_edit_paste(void);
extern void groove_edit_poll(void);
extern void groove_edit_record(void);
extern void groove_edit_recording(void);
extern void groove_edit_repeat(void);
extern void groove_edit_revision(void);
extern void groove_edit_seek(void);
extern void groove_edit_track_reset(void);
extern void groove_edit_undo(void);
extern void groove_effects_edit(void);
extern void groove_effects_get(void);
extern void groove_effects_restore(void);
extern void groove_fm_copy(void);
extern void groove_fm_edit(void);
extern void groove_fm_import(void);
extern void groove_fm_name_edit(void);
extern void groove_get_view(void);
extern void groove_key_note(void);
extern void groove_lfo_copy(void);
extern void groove_lfo_edit(void);
extern void groove_lfo_rate(void);
extern void groove_lock_parameter_count(void);
extern void groove_lock_parameter_name(void);
extern void groove_master_state(void);
extern void groove_master_update(void);
extern void groove_midi_activity(void);
extern void groove_midi_input_channel(void);
extern void groove_midi_input_set(void);
extern void groove_mix_toggle(void);
extern void groove_mono_set(void);
extern void groove_operator_toggle(void);
extern void groove_pad_count(void);
extern void groove_pad_index(void);
extern void groove_pad_label(void);
extern void groove_pad_name(void);
extern void groove_pad_note(void);
extern void groove_project_apply(void);
extern void groove_project_apply_edit(void);
extern void groove_project_arp_defaults(void);
extern void groove_project_capture(void);
extern void groove_project_clean(void);
extern void groove_project_dirty(void);
extern void groove_project_touch(void);
extern void groove_project_valid(void);
extern void groove_samples_stop(void);
extern void groove_sends_edit(void);
extern void groove_sends_get(void);
extern void groove_sequence_chord(void);
extern void groove_sequence_chord_name(void);
extern void groove_sequence_copy(void);
extern void groove_sequence_count_in(void);
extern void groove_sequence_defaults(void);
extern void groove_sequence_edit(void);
extern void groove_sequence_init(void);
extern void groove_sequence_keep_transport(void);
extern void groove_sequence_length(void);
extern void groove_sequence_length_set(void);
extern void groove_sequence_options_copy(void);
extern void groove_sequence_options_set(void);
extern void groove_sequence_read(void);
extern void groove_sequence_record_position(void);
extern void groove_sequence_replace(void);
extern void groove_sequence_restore(void);
extern void groove_sequence_rewound(void);
extern void groove_sequence_setting(void);
extern void groove_sequence_setting_edit(void);
extern void groove_sequence_source_changed(void);
extern void groove_sequence_valid(void);
extern void groove_sequence_view(void);
extern void groove_source_set(void);
extern void groove_source_type(void);
extern void groove_step_setting(void);
extern void groove_step_setting_edit(void);
extern void groove_tempo_current(void);
extern void groove_tempo_edit(void);
extern void groove_tempo_saved(void);
extern void groove_tempo_tap(void);
extern void groove_timbre_apply(void);
extern void groove_timbre_export(void);
extern void groove_timbre_import(void);
extern void groove_tone_copy(void);
extern void groove_tone_count(void);
extern void groove_tone_edit(void);
extern void groove_tone_parameter(void);
extern void groove_track_role(void);
extern void groove_track_role_set(void);
extern void groove_tuning_alt_hold(void);
extern void groove_tuning_apply(void);
extern void groove_tuning_capture(void);
extern void groove_tuning_get(void);
extern void groove_tuning_set(void);
extern void lib_btctler_version(void);
extern void lib_cfg_tool_version(void);
extern void lib_common_version(void);
extern void lib_crcm_version(void);
extern void lib_cryto_toolbox_version(void);
extern void lib_driver_version(void);
extern void lib_event_version(void);
extern void lib_fs_version(void);
extern void lib_system_version(void);
extern void lib_update_version(void);
extern void lib_wl82_rf_version(void);
extern void malloc(void);
extern void realloc(void);
extern void sampledrum_bind(void);
extern void sampledrum_param(void);
extern void sbc_decoder(void);
extern void sdfile_vfs_ops(void);
extern void sprintf(void);
extern void strdup(void);

const void *const keep_engine_linked[] __attribute__((used)) = {
    __errno,
    _start,
    abort,
    audio_app_bluetooth_open,
    bass303_param,
    bt_get_local_name,
    bt_get_mac_addr,
    calloc,
    clock_port,
    clock_timer1,
    connectivity_wl82_bluetooth_start,
    drum808_nparams,
    drum808_param,
    ecvtbuf,
    fcvtbuf,
    fm1_app_midi_received,
    fm1_app_sysex,
    fm1_ble_connect,
    fm1_ble_disconnect,
    fm1_ble_forget_bonds,
    fm1_ble_get_device,
    fm1_ble_get_status,
    fm1_ble_scan,
    fm1_ble_set_role,
    fm1_bluetooth_audio_read,
    fm1_bluetooth_configure,
    fm1_bluetooth_get_status,
    fm1_control_request,
    fm1_device_adc,
    fm1_device_adc_register,
    fm1_device_clock,
    fm1_device_delay,
    fm1_device_fence,
    fm1_device_irq_attach,
    fm1_device_irq_restore,
    fm1_device_irq_save,
    fm1_device_recovery,
    fm1_device_ticks,
    fm1_device_usb_connected,
    fm1_engine_output_level,
    fm1_engine_pause,
    fm1_engine_render,
    fm1_engine_resume,
    fm1_engine_stats_read,
    fm1_engine_stop,
    fm1_flash_erase,
    fm1_flash_invalidate,
    fm1_flash_map,
    fm1_flash_read,
    fm1_flash_size,
    fm1_flash_write,
    fm1_io_trs_midi_enabled,
    fm1_midi_input_ports,
    fm1_midi_input_source,
    fm1_midi_output_ports,
    fm1_midi_post,
    fm1_midi_ready_ports,
    fm1_midi_send,
    fm1_midi_send_port,
    fm1_millis,
    fm1_r_f4157fee5abb,
    fm1_sea_decode,
    fm1_sea_decode_groups,
    fm1_usb_audio_read,
    fm1_usb_audio_write,
    free,
    groove_adsr_edit,
    groove_adsr_enable,
    groove_arp_edit,
    groove_arp_enabled,
    groove_arp_set,
    groove_arp_setting,
    groove_arrangement_capture,
    groove_arrangement_tuning_apply,
    groove_arrangement_tuning_capture,
    groove_audio_stats,
    groove_clock_mode,
    groove_clock_set,
    groove_control_edit,
    groove_delay_ticks,
    groove_drum_index,
    groove_drum_note,
    groove_drum_steps,
    groove_edit_checkpoint,
    groove_edit_clear,
    groove_edit_clipboard_count,
    groove_edit_copy,
    groove_edit_cursor,
    groove_edit_error,
    groove_edit_forget_history,
    groove_edit_init,
    groove_edit_paste,
    groove_edit_poll,
    groove_edit_record,
    groove_edit_recording,
    groove_edit_repeat,
    groove_edit_revision,
    groove_edit_seek,
    groove_edit_track_reset,
    groove_edit_undo,
    groove_effects_edit,
    groove_effects_get,
    groove_effects_restore,
    groove_fm_copy,
    groove_fm_edit,
    groove_fm_import,
    groove_fm_name_edit,
    groove_get_view,
    groove_key_note,
    groove_lfo_copy,
    groove_lfo_edit,
    groove_lfo_rate,
    groove_lock_parameter_count,
    groove_lock_parameter_name,
    groove_master_state,
    groove_master_update,
    groove_midi_activity,
    groove_midi_input_channel,
    groove_midi_input_set,
    groove_mix_toggle,
    groove_mono_set,
    groove_operator_toggle,
    groove_pad_count,
    groove_pad_index,
    groove_pad_label,
    groove_pad_name,
    groove_pad_note,
    groove_project_apply,
    groove_project_apply_edit,
    groove_project_arp_defaults,
    groove_project_capture,
    groove_project_clean,
    groove_project_dirty,
    groove_project_touch,
    groove_project_valid,
    groove_samples_stop,
    groove_sends_edit,
    groove_sends_get,
    groove_sequence_chord,
    groove_sequence_chord_name,
    groove_sequence_copy,
    groove_sequence_count_in,
    groove_sequence_defaults,
    groove_sequence_edit,
    groove_sequence_init,
    groove_sequence_keep_transport,
    groove_sequence_length,
    groove_sequence_length_set,
    groove_sequence_options_copy,
    groove_sequence_options_set,
    groove_sequence_read,
    groove_sequence_record_position,
    groove_sequence_replace,
    groove_sequence_restore,
    groove_sequence_rewound,
    groove_sequence_setting,
    groove_sequence_setting_edit,
    groove_sequence_source_changed,
    groove_sequence_valid,
    groove_sequence_view,
    groove_source_set,
    groove_source_type,
    groove_step_setting,
    groove_step_setting_edit,
    groove_tempo_current,
    groove_tempo_edit,
    groove_tempo_saved,
    groove_tempo_tap,
    groove_timbre_apply,
    groove_timbre_export,
    groove_timbre_import,
    groove_tone_copy,
    groove_tone_count,
    groove_tone_edit,
    groove_tone_parameter,
    groove_track_role,
    groove_track_role_set,
    groove_tuning_alt_hold,
    groove_tuning_apply,
    groove_tuning_capture,
    groove_tuning_get,
    groove_tuning_set,
    lib_btctler_version,
    lib_cfg_tool_version,
    lib_common_version,
    lib_crcm_version,
    lib_cryto_toolbox_version,
    lib_driver_version,
    lib_event_version,
    lib_fs_version,
    lib_system_version,
    lib_update_version,
    lib_wl82_rf_version,
    malloc,
    realloc,
    sampledrum_bind,
    sampledrum_param,
    sbc_decoder,
    sdfile_vfs_ops,
    sprintf,
    strdup,
};
#endif
