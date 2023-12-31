sh = find_program('sh', native: true)

scdoc = dependency('scdoc', native: true)
scdoc_prog = find_program(scdoc.get_variable('scdoc'), native: true)

plugin_pages = []
if plugin_alsa_enabled
  plugin_pages += ['yambar-modules-alsa.5.scd']
endif
if plugin_backlight_enabled
  plugin_pages += ['yambar-modules-backlight.5.scd']
endif
if plugin_battery_enabled
  plugin_pages += ['yambar-modules-battery.5.scd']
endif
if plugin_clock_enabled
  plugin_pages += ['yambar-modules-clock.5.scd']
endif
if plugin_cpu_enabled
  plugin_pages += ['yambar-modules-cpu.5.scd']
endif
if plugin_disk_io_enabled
  plugin_pages += ['yambar-modules-disk-io.5.scd']
endif
if plugin_dwl_enabled
  plugin_pages += ['yambar-modules-dwl.5.scd']
endif
if plugin_foreign_toplevel_enabled
  plugin_pages += ['yambar-modules-foreign-toplevel.5.scd']
endif
if plugin_mem_enabled
  plugin_pages += ['yambar-modules-mem.5.scd']
endif
if plugin_mpd_enabled
  plugin_pages += ['yambar-modules-mpd.5.scd']
endif
if plugin_i3_enabled
  plugin_pages += ['yambar-modules-i3.5.scd']
endif
if plugin_label_enabled
  plugin_pages += ['yambar-modules-label.5.scd']
endif
if plugin_network_enabled
  plugin_pages += ['yambar-modules-network.5.scd']
endif
if plugin_pipewire_enabled
  plugin_pages += ['yambar-modules-pipewire.5.scd']
endif
if plugin_pulse_enabled
  plugin_pages += ['yambar-modules-pulse.5.scd']
endif
if plugin_removables_enabled
  plugin_pages += ['yambar-modules-removables.5.scd']
endif
if plugin_river_enabled
  plugin_pages += ['yambar-modules-river.5.scd']
endif
if plugin_script_enabled
  plugin_pages += ['yambar-modules-script.5.scd']
endif
if plugin_sway_xkb_enabled
  plugin_pages += ['yambar-modules-sway-xkb.5.scd']
endif
if plugin_xkb_enabled
  plugin_pages += ['yambar-modules-xkb.5.scd']
endif

foreach man_src : ['yambar.1.scd',
                   'yambar.5.scd',
                   'yambar-decorations.5.scd',
                   'yambar-modules.5.scd',
                   'yambar-particles.5.scd',
                   'yambar-tags.5.scd'] + plugin_pages
  parts = man_src.split('.')
  name = parts[-3]
  section = parts[-2]
  out = '@0@.@1@'.format(name, section)

  custom_target(
    out,
    output: out,
    input: man_src,
    command: [sh, '-c', '@0@ < @INPUT@'.format(scdoc_prog.full_path())],
    capture: true,
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man@0@'.format(section)))
endforeach
