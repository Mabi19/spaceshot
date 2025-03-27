project(
    'spaceshot',
    'c',
    version: '0.1',
    default_options: ['c_std=c23', 'warning_level=3'],
    meson_version: '>=1.1',
)

build_conf = configuration_data()
build_conf.set_quoted('SPACESHOT_VERSION', meson.project_version())
build_conf.set(
    'SPACESHOT_TIMING',
    get_option('timing'),
    description: 'Record timing information'
)
build_conf.set(
    'SPACESHOT_DEBUG_CLIPPING',
    get_option('debug-clipping'),
    description: 'Draw clip regions on screen instead of applying them'
)

configure_file(
    output: 'build-config.h',
    configuration: build_conf,
)
build_conf_include = include_directories('.')

subdir('src')
