# RidgeKey - daemon and command-line client
# SPDX-License-Identifier: MIT
project('ridgekey', 'c',
  version: '0.2.0',
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

executable('ridgekeyd',
  ['daemon/ridgekeyd.c', 'daemon/store.c'],
  include_directories: common_inc,
  dependencies: [glib_dep, gio_dep, libfprint_dep],
  install: true)

# The client only needs libSystem (XPC + libdispatch).
executable('ridgekeyctl',
  ['ctl/ridgekeyctl.c'],
  include_directories: common_inc,
  install: true)

# PAM module for sudo (and later other services). Loaded by sudo from an absolute path.
pam_dep = cc.find_library('pam')
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
