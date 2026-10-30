# RidgeKey - daemon and command-line client
# SPDX-License-Identifier: MIT
project('ridgekey', 'c',
  version: '0.4.1',
  license: 'MIT',
  meson_version: '>= 0.59.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized'])

if host_machine.system() != 'darwin'
  error('RidgeKey targets macOS (XPC, launchd)')
endif

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments([
  '-fblocks',
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  '-Werror=implicit-function-declaration',
  '-Werror=incompatible-pointer-types',
  '-Werror=return-type',
]), language: 'c')

common_inc = include_directories('common')

glib_dep     = dependency('glib-2.0', version: '>= 2.68')
gio_dep      = dependency('gio-2.0', version: '>= 2.68')
libfprint_dep = dependency('libfprint-2')

pam_dep = cc.find_library('pam')
bsm_dep = cc.find_library('bsm')     # audit_token_to_pid / _euid (peer.c)
daemon_frameworks = dependency('appleframeworks', modules: ['CoreFoundation', 'IOKit', 'Security'])

executable('ridgekeyd',
  ['daemon/ridgekeyd.c', 'daemon/store.c', 'daemon/secret.c', 'daemon/console.c', 'daemon/peer.c'],
  include_directories: common_inc,
  dependencies: [glib_dep, gio_dep, libfprint_dep, pam_dep, bsm_dep, daemon_frameworks],
  install: true)

# The client only needs libSystem (XPC + libdispatch).
executable('ridgekeyctl',
  ['ctl/ridgekeyctl.c'],
  include_directories: common_inc,
  install: true)

# PAM module for sudo (and later other services). Loaded by sudo from an absolute path.
shared_module('pam_ridgekey',
  ['pam/pam_ridgekey.c'],
  include_directories: common_inc,
  dependencies: [pam_dep],
  name_prefix: '',
  name_suffix: 'so',
  install: true,
  install_dir: get_option('libdir') / 'pam')

executable('ridgekey-pamcheck',
  ['tools/ridgekey-pamcheck.c'],
  dependencies: [pam_dep],
  install: true)

# Feasibility test for lock-screen password typing (types a dummy word, never Return).
appsvc_dep = dependency('appleframeworks', modules: ['ApplicationServices', 'CoreFoundation'])
executable('ridgekey-typetest',
  ['tools/ridgekey-typetest.c'],
  dependencies: [appsvc_dep],
  install: true)

# Lock-screen agent: per-user LaunchAgent. System frameworks only (it is signed with the
# hardened runtime, which ridgekeyd checks before releasing the password to it).
executable('ridgekey-agent',
  ['agent/ridgekey-agent.c'],
  include_directories: common_inc,
  dependencies: [appsvc_dep],
  install: true)
