pretracker = static_library('pretracker',
  'pretracker/pretracker.c',
  'pretracker/pretracker_mixer.c',
  'pretracker/pretracker_parse.c',
  'pretracker/pretracker_sequencer.c',
  'pretracker/pretracker_tables.c',
  'pretracker/pretracker_wavegen.c',
  c_args: common_flags, dependencies: math, gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0'])
replay_libs += pretracker
