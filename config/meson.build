python = find_program('python3')
config_script = files('config.py')

# [0] = .c, [1] = .h, [2] = .vapi
config_generated = custom_target(
    'spaceshot-config-generated',
    output: [
        'spaceshot-config-loader.c',
        'spaceshot-config-struct-decl.h',
        'spaceshot-config.vapi',
    ],
    input: files('config.py', 'generator.py'),
    command: [python, config_script, '@OUTPUT@'],
)

regular_sources = [
    'parse.c',
    'config.c',
]

config_lib = static_library(
    'spaceshot-config',
    [
        regular_sources,
        config_generated[0],
        config_generated[1],
    ],
    vala_header: 'config/config.h',
    vala_vapi: config_generated[2].full_path(),
)

config_dep = declare_dependency(sources: [config_generated[1]], link_with: config_lib)
config_dep_vala = declare_dependency(
    sources: [config_generated[1], config_generated[2]],
    link_with: config_lib,
    include_directories: '.',
)
