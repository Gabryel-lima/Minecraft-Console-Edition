exclude_sources = [
  ' ! -path "*/Platform/*"', # added by platform_sources
  ' ! -name "SurvivalMode.cpp"',
  ' ! -name "CreativeMode.cpp"',
  ' ! -name "GameMode.cpp"',
  ' ! -name "DemoMode.cpp"',
]

# all sources except ./Platform/*
client_sources = run_command(
  'sh',
  '-c', 'find "'
  + meson.current_source_dir()
  + '" \\( -name "*.cpp" -o -name "*.c" \\)'
  + ' '.join(exclude_sources),
  check: true,
).stdout().strip().split('\n')

# all sources in ./Platform (top-level files only)
platform_sources = run_command(
  'sh',
  '-c', 'find "'
  + meson.current_source_dir() / 'Platform'
  + '" -maxdepth 1 \\( -name "*.cpp" -o -name "*.c" \\)',
  check: true,
).stdout().strip().split('\n')

# some platform-specific sources that are for some stupid reason in Common
exclude_platform_common_sources = [
  ' ! -path "*/Network/Sony/*"',
  ' ! -path "*/XUI/*"',
  ' ! -name "SonyLeaderboardManager.cpp"',
  ' ! -name "UIScene_InGameSaveManagementMenu.cpp"',
]

# all sources in in ./Platform/Common
platform_sources += run_command(
  'sh',
  '-c', 'find "'
  + meson.current_source_dir() / 'Platform/Common'
  + '" \\( -name "*.cpp" -o -name "*.c" \\)'
  + ' '.join(exclude_platform_common_sources),
  check: true,
).stdout().strip().split('\n')

# linux-specific files (everything in Platform/Linux)
if host_machine.system() == 'linux'
  platform_sources += run_command(
    'sh',
    '-c', 'find "'
    + meson.current_source_dir() / 'Platform/Linux'
    + '" \\( -name "*.cpp" -o -name "*.c" \\) ',
    check: true,
  ).stdout().strip().split('\n')
endif

client_dependencies = [
  render_dep,
  input_dep,
  profile_dep,
  storage_dep,
  assets_localisation_dep,
  world_dep,
  gl_dep,
  glu_dep,
  thread_dep,
  dl_dep,
  dependency('zlib'),
  miniaudio_dep,
  stb_dep,
]

assets_copy = custom_target(
  'copy_assets_to_client',
  input: media_archive,
  output: 'assets.stamp', # using a stamp file to avoid copying assets every time
  command: [
    python,
    meson.project_source_root() / 'scripts/copy_assets_to_client.py',
    meson.project_source_root(),
    meson.project_build_root(),
    meson.current_build_dir(),
    '@INPUT0@',
    '@OUTPUT@',
  ],
  build_by_default: true,
)

if get_option('enable_vsync')
  global_cpp_defs += ['-DENABLE_VSYNC']
endif

if get_option('classic_panorama')
    global_cpp_defs += '-DCLASSIC_PANORAMA'
endif

if get_option('enable_frame_profiler')
  global_cpp_defs += ['-DENABLE_FRAME_PROFILER']
endif

ui_backend = get_option('ui_backend')
resolved_ui_backend = ui_backend

# `shiggy` contains AVX-heavy code; fall back to the Java GUI backend on
# Linux hosts that do not advertise AVX support.
if ui_backend == 'shiggy' and host_machine.system() == 'linux'
  has_avx = run_command(
    'sh',
    '-c', 'grep -m1 "^flags" /proc/cpuinfo | grep -qw avx',
    check: false,
  ).returncode() == 0

  if not has_avx
    message('AVX not detected; falling back to the Java UI backend.')
    resolved_ui_backend = 'java'
  endif
endif

if resolved_ui_backend == 'shiggy'
  shiggy_dep = dependency(
    'shiggy',
    fallback: ['shiggy', 'shiggy_dep'],
  )

  global_cpp_defs += ['-D_ENABLEIGGY']
  client_dependencies += shiggy_dep
endif

if resolved_ui_backend == 'java'
  global_cpp_defs += '-DENABLE_JAVA_GUIS'
endif

occlusion_mode = get_option('occlusion_culling')
if occlusion_mode == 'off'
  global_cpp_defs += ['-DOCCLUSION_MODE_NONE']
elif occlusion_mode == 'frustum'
  global_cpp_defs += ['-DOCCLUSION_MODE_FRUSTUM']
elif occlusion_mode == 'bfs'
  global_cpp_defs += ['-DOCCLUSION_MODE_BFS', '-DUSE_OCCLUSION_CULLING']
elif occlusion_mode == 'hardware'
  global_cpp_defs += ['-DOCCLUSION_MODE_HARDWARE', '-DUSE_OCCLUSION_CULLING']
endif
client = executable(
  'Minecraft.Client',
  client_sources + platform_sources + localisation[1],
  include_directories: include_directories('Platform', 'Platform/Linux/Iggy/include'),
  dependencies: client_dependencies,
  link_depends: assets_copy,
  cpp_args: global_cpp_args
  + global_cpp_defs
  + [
    '-DUNICODE',
    '-D_UNICODE',
    '-include', meson.current_source_dir() / 'Platform/stdafx.h',
  ],
  c_args: global_cpp_defs + ['-DUNICODE', '-D_UNICODE'],
  install: true,
  install_dir: '',
)
