project('cromagrally-server', 'cpp',
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
  meson_version: '>=1.0.0',
)

#------------------------------------------------------------------------------
# DEPENDENCIES
#------------------------------------------------------------------------------

# GameNetworkingSockets (provided by Conan or system pkg-config)
# Conan uses 'GameNetworkingSockets', system may use lowercase
gns_dep = dependency('gamenetworkingsockets', required: false)
if not gns_dep.found()
  gns_dep = dependency('GameNetworkingSockets', required: true)
endif

# spdlog (provided by Conan or system pkg-config)
spdlog_dep = dependency('spdlog', required: true)

# Threads
threads_dep = dependency('threads', required: true)

#------------------------------------------------------------------------------
# SOURCE FILES
#------------------------------------------------------------------------------

server_sources = files(
  'src/main.cpp',
  'src/relay/Server.cpp',
  'src/relay/Room.cpp',
)

#------------------------------------------------------------------------------
# INCLUDE DIRECTORIES
#------------------------------------------------------------------------------

server_includes = include_directories(
  'src',
  '..',  # For common/ headers
)

#------------------------------------------------------------------------------
# COMPILER FLAGS
#------------------------------------------------------------------------------

cxx = meson.get_compiler('cpp')

server_args = []
if cxx.get_id() != 'msvc'
  server_args += [
    '-Wall',
    '-Wextra',
    '-Wpedantic',
  ]
endif

#------------------------------------------------------------------------------
# EXECUTABLE
#------------------------------------------------------------------------------

executable('relay-server',
  server_sources,
  include_directories: server_includes,
  cpp_args: server_args,
  dependencies: [gns_dep, spdlog_dep, threads_dep],
  install: true,
)

#------------------------------------------------------------------------------
# SUMMARY
#------------------------------------------------------------------------------

summary({
  'Version': meson.project_version(),
  'Build type': get_option('buildtype'),
}, section: 'Project')

summary({
  'GameNetworkingSockets': gns_dep.found(),
  'spdlog': spdlog_dep.found(),
}, section: 'Dependencies')
