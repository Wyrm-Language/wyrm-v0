cfg = configuration_data()

doctest_dep = dependency('doctest', fallback: ['doctest', 'doctest_dep'], required: get_option('tests'))
glib_dep = dependency('glib-2.0', version: '>=2.56', required: get_option('glib'))


# -----------------------------------------------------------------------------
# Core Sources
# -----------------------------------------------------------------------------

cwyrm_lib_inc = include_directories('.', '../include')

cwyrm_sources = files(
    'bson.c',
    'context.c',
    'fiber.c',
    'gc.c',
    'image.c',
    'machine.c',
    'module.c',
    'pair.c',
    'slot.c',
    'wbox.c',
    'wclass.c',
    'wdict.c',
    'wobject.c',
    'string.c',
    'wtype.c',
    'vm.c',
)

# Each modules module sets a dict named with keys:
#   sources, inc, deps, public_inc, test_sources, test_inc, test_deps
modules = []

# -----------------------------------------------------------------------------
# Platform / OS Abstractions
# -----------------------------------------------------------------------------

subdir('platform/common')     # sets platform_common; always enabled
modules += platform_common

if get_option('hosted')
    cfg.set('WY_HAVE_HOSTED', 1)

    subdir('platform/hosted')
    modules += platform_hosted

    subdir('ports/hosted')
    modules += ports_hosted
endif

if glib_dep.found()
    cfg.set('WY_HAVE_GLIB', 1)
    subdir('platform/glib')
    modules += platform_glib
endif

wy_opt = get_option('wyrm')

# -----------------------------------------------------------------------------
# Build Optional / Extensions to Core
# -----------------------------------------------------------------------------

extra_sources    = []
extra_inc        = []
extra_deps       = []
extra_public_inc = []

extra_test_sources = []
extra_test_inc     = []
extra_test_deps    = []

foreach p : modules
    extra_sources    += p['sources']
    extra_inc        += p['inc']
    extra_deps       += p['deps']
    extra_public_inc += p['public_inc']

    extra_test_sources += p['test_sources']
    extra_test_inc     += p['test_inc']
    extra_test_deps    += p['test_deps']
endforeach

# -----------------------------------------------------------------------------
# Create Configuration File
# -----------------------------------------------------------------------------

configure_file(
    input: 'wyrm_config.h.in',
    output: 'wyrm_config.h',
    configuration: cfg,
    install: true,
    install_dir: get_option('includedir') / 'wyrm',
)

# Core Wyrm Extensions
subdir('builtin')

# -----------------------------------------------------------------------------
# Construct Library
# -----------------------------------------------------------------------------

cwyrm_lib = library('cwyrm',
    cwyrm_sources + extra_sources,
    include_directories: [cwyrm_lib_inc] + extra_inc,
    dependencies: extra_deps,
    install: true,
)

cwyrm_lib_dep = declare_dependency(
    link_with: cwyrm_lib,
    include_directories: [include_directories('../include')] + extra_public_inc,
    dependencies: extra_deps,
)

install_headers('../include/wyrm.h')
install_subdir('../include/wyrm', install_dir: get_option('includedir'))

pkgconfig = import('pkgconfig')
pkgconfig.generate(cwyrm_lib,
    name:        'cwyrm',
    description: 'Wyrm C runtime library',
    version:     meson.project_version(),
)

if not wy_opt.disabled()
    subdir('wyrm')
endif

cxx_bindings = get_option('cxx_bindings')
if not cxx_bindings.disabled()
    subdir('wyrmxx')
endif

tests_opt = get_option('tests')
if not tests_opt.disabled()
    if cxx_bindings.disabled()
        error('tests option requires cxx_bindings to be enabled')
    endif
    subdir('test')
endif

summary({
    'hosted'       : get_option('hosted'),
    'glib'         : glib_dep.found(),
    'cxx_bindings' : not cxx_bindings.disabled(),
    'tests'        : not tests_opt.disabled(),
#    'wy'     : not wy_opt.disabled(),
#    'wyrm'   : not wy_opt.disabled(),
}, section: 'Configuration', bool_yn: true)
