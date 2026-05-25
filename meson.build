project('cromagrally', 'c', 'cpp',
  version: '1.11.0',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=c17',
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
  meson_version: '>= 1.0.0',
  subproject_dir: 'extern',
)

# Project metadata
game_target = 'CroMagRally'
game_full_name = 'Cro-Mag Rally'
game_identifier = 'io.jor.cromagrally'
game_copyright = '© 2000 Pangea Software, Inc., © 2025 Iliyas Jorio.'

# Directories
src_dir = 'Source'
data_dir = 'Data'

# Imports
fs = import('fs')

# Compilers
cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

#------------------------------------------------------------------------------
# OPTIONS
#------------------------------------------------------------------------------

opt_sanitize = get_option('sanitize')
opt_sdl_static = get_option('sdl_static')

#------------------------------------------------------------------------------
# DEPENDENCIES
#------------------------------------------------------------------------------

# OpenGL (use system method for Windows cross-compile compatibility)
if host_machine.system() == 'windows'
  opengl_dep = cc.find_library('opengl32')
else
  opengl_dep = dependency('opengl', required: true)
endif

# SDL3 (provided by Conan or system pkg-config)
# Conan uses 'sdl', system packages use 'sdl3'
sdl3_dep = dependency('sdl3', required: false, static: opt_sdl_static)
if not sdl3_dep.found()
  sdl3_dep = dependency('sdl', required: true, static: opt_sdl_static)
endif

# GameNetworkingSockets (provided by Conan or system pkg-config)
# Conan uses 'GameNetworkingSockets', system may use lowercase
gns_dep = dependency('gamenetworkingsockets', required: false)
if not gns_dep.found()
  gns_dep = dependency('GameNetworkingSockets', required: true)
endif

# spdlog (provided by Conan or system pkg-config)
spdlog_dep = dependency('spdlog', required: true)

# Math library (Linux)
m_dep = cc.find_library('m', required: false)

# Windows socket library
ws2_32_dep = cc.find_library('ws2_32', required: host_machine.system() == 'windows')

#------------------------------------------------------------------------------
# POMME SUBPROJECT
#------------------------------------------------------------------------------

# Configure Pomme options before including subproject
pomme_options = [
  'pomme_no_graphics=true',
  'pomme_no_input=true',
  'pomme_no_mp3=true',
  'pomme_no_qd3d=true',
  'pomme_no_video=true',
]

pomme_proj = subproject('pomme',
  default_options: pomme_options,
)
pomme_dep = pomme_proj.get_variable('pomme_dep')

#------------------------------------------------------------------------------
# VERSION HEADER
#------------------------------------------------------------------------------

version_parts = meson.project_version().split('.')

version_conf = configuration_data()
version_conf.set('GAME_TARGET', game_target)
version_conf.set('GAME_FULL_NAME', game_full_name)
version_conf.set('GAME_IDENTIFIER', game_identifier)
version_conf.set('PROJECT_VERSION', meson.project_version())
version_conf.set('PROJECT_VERSION_MAJOR', version_parts[0])
version_conf.set('PROJECT_VERSION_MINOR', version_parts[1])
version_conf.set('PROJECT_VERSION_PATCH', version_parts[2])

configure_file(
  input: src_dir / 'Headers' / 'version.h.in',
  output: 'version.h',
  configuration: version_conf,
)

#------------------------------------------------------------------------------
# SOURCE FILES
#------------------------------------------------------------------------------

game_sources = files(
  # 3D
  src_dir / '3D' / '3DMath.c',
  src_dir / '3D' / 'Atlas.c',
  src_dir / '3D' / 'Camera.c',
  src_dir / '3D' / 'Effects.c',
  src_dir / '3D' / 'MetaObjects.c',
  src_dir / '3D' / 'OGL_Support.c',
  src_dir / '3D' / 'Pillarbox.c',
  src_dir / '3D' / 'QuadMesh.c',
  src_dir / '3D' / 'Sprites.c',
  src_dir / '3D' / 'bg3d.c',

  # Items
  src_dir / 'Items' / 'Items.c',
  src_dir / 'Items' / 'Traps.c',
  src_dir / 'Items' / 'Triggers.c',

  # Player
  src_dir / 'Player' / 'Player.c',
  src_dir / 'Player' / 'Player_Car.c',
  src_dir / 'Player' / 'Player_Submarine.c',
  src_dir / 'Player' / 'Player_Weapons.c',
  src_dir / 'Player' / 'SkidMarks.c',

  # Screens
  src_dir / 'Screens' / 'Failed.c',
  src_dir / 'Screens' / 'Infobar.c',
  src_dir / 'Screens' / 'LocalGather.c',
  src_dir / 'Screens' / 'MainMenu.c',
  src_dir / 'Screens' / 'MiscScreens.c',
  src_dir / 'Screens' / 'NetworkGather.c',
  src_dir / 'Screens' / 'Notifications.c',
  src_dir / 'Screens' / 'Paused.c',
  src_dir / 'Screens' / 'PhysicsEditor.c',
  src_dir / 'Screens' / 'RaceTimes.c',
  src_dir / 'Screens' / 'SelectCharacter.c',
  src_dir / 'Screens' / 'SelectTrack.c',
  src_dir / 'Screens' / 'SelectVehicle.c',
  src_dir / 'Screens' / 'Settings.c',
  src_dir / 'Screens' / 'UIEffects.c',

  # Skeleton
  src_dir / 'Skeleton' / 'Bones.c',
  src_dir / 'Skeleton' / 'SkeletonAnim.c',
  src_dir / 'Skeleton' / 'SkeletonJoints.c',
  src_dir / 'Skeleton' / 'SkeletonObj.c',

  # System
  src_dir / 'System' / 'Collision.c',
  src_dir / 'System' / 'File.c',
  src_dir / 'System' / 'Input.c',
  src_dir / 'System' / 'InputControlBits.c',
  src_dir / 'System' / 'InputDefaults.c',
  src_dir / 'System' / 'Localization.c',
  src_dir / 'System' / 'LZSS.c',
  src_dir / 'System' / 'Main.c',
  src_dir / 'System' / 'Menu.c',
  src_dir / 'System' / 'Misc.c',
  src_dir / 'System' / 'Objects.c',
  src_dir / 'System' / 'Objects2.c',
  src_dir / 'System' / 'Sound.c',
  src_dir / 'System' / 'stb_image.c',
  src_dir / 'System' / 'Window.c',
  src_dir / 'System' / 'network.cpp',

  # Terrain
  src_dir / 'Terrain' / 'Checkpoints.c',
  src_dir / 'Terrain' / 'Fences.c',
  src_dir / 'Terrain' / 'Liquids.c',
  src_dir / 'Terrain' / 'Paths.c',
  src_dir / 'Terrain' / 'SplineItems.c',
  src_dir / 'Terrain' / 'Terrain.c',
  src_dir / 'Terrain' / 'Terrain2.c',

  # Backend
  src_dir / 'Backend' / 'Backend_GNS.cpp',

  # Boot
  src_dir / 'Boot.cpp',
)

#------------------------------------------------------------------------------
# INCLUDE DIRECTORIES
#------------------------------------------------------------------------------

game_includes = include_directories(
  src_dir / 'Headers',
  src_dir / 'Backend',
  '.',
)

#------------------------------------------------------------------------------
# COMPILE DEFINITIONS
#------------------------------------------------------------------------------

game_defines = [
  '-DHQ_TERRAIN=1',
  '-DENABLE_LOADING_SCREEN=0',
  '-DGL_SILENCE_DEPRECATION',
  '-DUSE_GNS=1',
  '-DPOMME_CASE_SENSITIVE_FSSPEC=1',
]

if get_option('debug')
  game_defines += '-D_DEBUG'
endif

if host_machine.system() == 'windows'
  game_defines += [
    '-DWIN32_LEAN_AND_MEAN',
    '-DNOGDI',
    '-DNOUSER',
  ]
endif

#------------------------------------------------------------------------------
# COMPILER FLAGS
#------------------------------------------------------------------------------

game_c_args = game_defines
game_cpp_args = game_defines

if cc.get_id() != 'msvc'
  common_args = [
    '-fexceptions',
    '-Wall',
    '-Wextra',
    '-Wshadow',
    '-Werror=return-type',
    '-Wno-multichar',
    '-Wno-unknown-pragmas',
    '-Wno-unused-parameter',
    '-Wstrict-aliasing=2',
  ]
  game_c_args += common_args + ['-Werror=incompatible-pointer-types']
  game_cpp_args += common_args

  if opt_sanitize
    sanitize_args = [
      '-fsanitize=alignment',
      '-fsanitize=address',
      '-fsanitize=leak',
      '-fsanitize=undefined',
      '-fno-omit-frame-pointer',
    ]
    game_c_args += sanitize_args
    game_cpp_args += sanitize_args
  endif
else
  msvc_args = [
    '/EHs',
    '/W4',
    '/wd4068',  # ignore unrecognized pragmas
    '/wd4100',  # unreferenced formal parameters
    '/wd4200',  # nonstandard extension (zero-sized array)
    '/wd4201',  # nonstandard extension (nameless struct)
    '/wd4244',  # conversion from double to float
    '/wd4305',  # truncation from double to float
    '/wd5105',  # macro expansion issue
    '/MP',      # multiprocessor build
    '/Zi',      # PDB output
    '/Zc:__cplusplus',
  ]
  game_c_args += msvc_args
  game_cpp_args += msvc_args
endif

#------------------------------------------------------------------------------
# LINK FLAGS
#------------------------------------------------------------------------------

game_link_args = []

if host_machine.system() == 'windows' and cc.get_id() != 'msvc'
  # MinGW: statically link GCC runtime
  game_link_args += ['-static-libgcc', '-static-libstdc++', '-static']
endif

if cc.get_id() == 'msvc'
  game_link_args += '/DEBUG'
endif

#------------------------------------------------------------------------------
# DEPENDENCIES LIST
#------------------------------------------------------------------------------

game_deps = [
  sdl3_dep,
  opengl_dep,
  pomme_dep,
  gns_dep,
  spdlog_dep,
  m_dep,
]

if host_machine.system() == 'windows'
  game_deps += ws2_32_dep
endif

# macOS frameworks
if host_machine.system() == 'darwin'
  game_deps += dependency('appleframeworks', modules: ['Foundation', 'IOKit'])
endif

# Sanitizer libraries
if opt_sanitize and cc.get_id() != 'msvc'
  asan_dep = cc.find_library('asan', required: false)
  ubsan_dep = cc.find_library('ubsan', required: false)
  if asan_dep.found()
    game_deps += asan_dep
  endif
  if ubsan_dep.found()
    game_deps += ubsan_dep
  endif
endif

#------------------------------------------------------------------------------
# WINDOWS RESOURCES
#------------------------------------------------------------------------------

windows_resources = []
if host_machine.system() == 'windows'
  windows = import('windows')
  # RC file is pre-configured by CMake, use the existing one
  if fs.is_file('packaging' / game_target + '.exe.rc')
    windows_resources = windows.compile_resources(
      'packaging' / game_target + '.exe.rc',
    )
  endif
endif

#------------------------------------------------------------------------------
# EXECUTABLE
#------------------------------------------------------------------------------

game_exe = executable(game_target,
  game_sources,
  windows_resources,
  include_directories: game_includes,
  c_args: game_c_args,
  cpp_args: game_cpp_args,
  link_args: game_link_args,
  dependencies: game_deps,
  win_subsystem: 'windows',
  install: true,
)

#------------------------------------------------------------------------------
# POST-BUILD: COPY DATA
#------------------------------------------------------------------------------

# Copy Data directory to build output
if host_machine.system() == 'darwin'
  # macOS: Copy to Resources inside .app bundle
  # Note: Meson doesn't have direct .app bundle support like CMake
  # For full macOS bundle, use a custom script or install rules
  data_install_dir = get_option('datadir') / game_target + '.app' / 'Contents' / 'Resources'
else
  data_install_dir = get_option('bindir') / 'Data'
endif

install_subdir(data_dir,
  install_dir: get_option('bindir'),
  strip_directory: false,
)

#------------------------------------------------------------------------------
# SUMMARY
#------------------------------------------------------------------------------

summary({
  'Game': game_full_name,
  'Version': meson.project_version(),
  'Build type': get_option('buildtype'),
  'Sanitizers': opt_sanitize,
}, section: 'Project')

summary({
  'SDL3': sdl3_dep.found() ? sdl3_dep.version() : 'subproject',
  'GameNetworkingSockets': gns_dep.found() ? 'found' : 'subproject',
  'spdlog': spdlog_dep.found() ? 'found' : 'subproject',
  'OpenGL': opengl_dep.found(),
}, section: 'Dependencies')
