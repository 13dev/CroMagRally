project('pomme', 'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ],
  meson_version: '>=1.0.0',
)

pomme_srcdir = 'src'

cxx = meson.get_compiler('cpp')

#------------------------------------------------------------------------------
# OPTIONS
#------------------------------------------------------------------------------

opt_no_graphics = get_option('pomme_no_graphics')
opt_no_input = get_option('pomme_no_input')
opt_no_mp3 = get_option('pomme_no_mp3')
opt_no_qd3d = get_option('pomme_no_qd3d')
opt_no_video = get_option('pomme_no_video')
opt_no_sound_formats = get_option('pomme_no_sound_formats')
opt_no_sound_mixer = get_option('pomme_no_sound_mixer')

#------------------------------------------------------------------------------
# SOURCE FILES
#------------------------------------------------------------------------------

pomme_sources = files(
  pomme_srcdir / 'Pomme.cpp',
  pomme_srcdir / 'PommeDebug.cpp',
  pomme_srcdir / 'Files' / 'Files.cpp',
  pomme_srcdir / 'Files' / 'HostVolume.cpp',
  pomme_srcdir / 'Files' / 'Resources.cpp',
  pomme_srcdir / 'Memory' / 'Memory.cpp',
  pomme_srcdir / 'Text' / 'TextUtilities.cpp',
  pomme_srcdir / 'Time' / 'TimeManager.cpp',
  pomme_srcdir / 'Utilities' / 'bigendianstreams.cpp',
  pomme_srcdir / 'Utilities' / 'IEEEExtended.cpp',
  pomme_srcdir / 'Utilities' / 'memstream.cpp',
  pomme_srcdir / 'Utilities' / 'StringUtils.cpp',
  pomme_srcdir / 'Utilities' / 'structpack.cpp',
)

# Windows-specific sources
if host_machine.system() == 'windows'
  pomme_sources += files(
    pomme_srcdir / 'Platform' / 'Windows' / 'PommeWindows.cpp',
  )
endif

# Sound formats
if not opt_no_sound_formats
  pomme_sources += files(
    pomme_srcdir / 'SoundFormats' / 'AIFF.cpp',
    pomme_srcdir / 'SoundFormats' / 'IMA4.cpp',
    pomme_srcdir / 'SoundFormats' / 'MACE.cpp',
    pomme_srcdir / 'SoundFormats' / 'Midi.cpp',
    pomme_srcdir / 'SoundFormats' / 'SoundFormats.cpp',
    pomme_srcdir / 'SoundFormats' / 'xlaw.cpp',
  )
endif

# MP3 support
if not opt_no_mp3
  pomme_sources += files(
    pomme_srcdir / 'SoundFormats' / 'mp3.cpp',
  )
endif

# Sound mixer
if not opt_no_sound_mixer
  pomme_sources += files(
    pomme_srcdir / 'SoundMixer' / 'ChannelImpl.cpp',
    pomme_srcdir / 'SoundMixer' / 'cmixer.cpp',
    pomme_srcdir / 'SoundMixer' / 'SoundManager.cpp',
  )
endif

# Graphics
if not opt_no_graphics
  pomme_sources += files(
    pomme_srcdir / 'Graphics' / 'ARGBPixmap.cpp',
    pomme_srcdir / 'Graphics' / 'Color.cpp',
    pomme_srcdir / 'Graphics' / 'ColorManager.cpp',
    pomme_srcdir / 'Graphics' / 'Graphics.cpp',
    pomme_srcdir / 'Graphics' / 'Icons.cpp',
    pomme_srcdir / 'Graphics' / 'PICT.cpp',
    pomme_srcdir / 'Graphics' / 'SystemPalettes.cpp',
  )
endif

# Input
if not opt_no_input
  pomme_sources += files(
    pomme_srcdir / 'Input' / 'SDLInput.cpp',
  )
endif

# Video
if not opt_no_video
  pomme_sources += files(
    pomme_srcdir / 'Video' / 'Cinepak.cpp',
    pomme_srcdir / 'Video' / 'moov.cpp',
  )
endif

# QD3D
if not opt_no_qd3d
  pomme_sources += files(
    pomme_srcdir / 'QD3D' / '3DMFParser.cpp',
    pomme_srcdir / 'QD3D' / 'QD3D.cpp',
    pomme_srcdir / 'QD3D' / 'QD3DMath.cpp',
  )
endif

#------------------------------------------------------------------------------
# COMPILE DEFINITIONS
#------------------------------------------------------------------------------

pomme_defines = []

if opt_no_sound_formats
  pomme_defines += '-DPOMME_NO_SOUND_FORMATS'
endif
if opt_no_mp3
  pomme_defines += '-DPOMME_NO_MP3'
endif
if opt_no_sound_mixer
  pomme_defines += '-DPOMME_NO_SOUND_MIXER'
endif
if opt_no_graphics
  pomme_defines += '-DPOMME_NO_GRAPHICS'
endif
if opt_no_input
  pomme_defines += '-DPOMME_NO_INPUT'
endif
if opt_no_video
  pomme_defines += '-DPOMME_NO_VIDEO'
endif
if opt_no_qd3d
  pomme_defines += '-DPOMME_NO_QD3D'
endif

if get_option('debug')
  pomme_defines += '-D_DEBUG'
endif

#------------------------------------------------------------------------------
# COMPILER FLAGS
#------------------------------------------------------------------------------

pomme_cpp_args = pomme_defines

if cxx.get_id() != 'msvc'
  pomme_cpp_args += [
    '-fexceptions',
    '-Wall',
    '-Wextra',
    '-Wshadow',
    '-Wno-multichar',
    '-Wno-unknown-pragmas',
    '-Wstrict-aliasing=2',
  ]
else
  pomme_cpp_args += [
    '/EHs',
    '/W4',
    '/wd4068',  # ignore unrecognized pragmas
    '/wd4201',  # nonstandard extension (nameless struct)
    '/wd4244',  # conversion from double to float
    '/wd4458',  # declaration of variable hides class member
    '/MP',
  ]

  if host_machine.system() == 'windows'
    pomme_cpp_args += [
      '-DWIN32_LEAN_AND_MEAN',
      '-DNOGDI',
      '-DNOUSER',
    ]
  endif
endif

#------------------------------------------------------------------------------
# DEPENDENCIES
#------------------------------------------------------------------------------

pomme_deps = []

# SDL3 required for Input and SoundManager
# Try multiple names: 'sdl3' (system), 'sdl' (Conan), 'SDL3' (CMake)
if not opt_no_input or not opt_no_sound_mixer
  sdl3_dep = dependency('sdl3', required: false)
  if not sdl3_dep.found()
    sdl3_dep = dependency('sdl', required: false)
  endif
  if not sdl3_dep.found()
    sdl3_dep = dependency('SDL3', required: true)
  endif
  pomme_deps += sdl3_dep
endif

#------------------------------------------------------------------------------
# INCLUDE DIRECTORIES
#------------------------------------------------------------------------------

pomme_includes = include_directories(pomme_srcdir)

pomme_public_includes = [pomme_includes]
if not opt_no_qd3d
  pomme_public_includes += include_directories(pomme_srcdir / 'QD3D')
endif

#------------------------------------------------------------------------------
# LIBRARY
#------------------------------------------------------------------------------

pomme_lib = static_library('Pomme',
  pomme_sources,
  include_directories: pomme_includes,
  cpp_args: pomme_cpp_args,
  dependencies: pomme_deps,
)

pomme_dep = declare_dependency(
  link_with: pomme_lib,
  include_directories: pomme_public_includes,
  dependencies: pomme_deps,
)
