python = find_program('python3')
config_script = files('config.py')

# [0] = .c, [1] = .h
config_generated = custom_target(
    'spaceshot-config-generated',
    output: ['spaceshot-config-loader.c', 'spaceshot-config-struct-decl.h'],
    input: files(['config.py', 'generator.py']),
    command: [python, config_script, '@OUTPUT@'],
)

regular_sources = [
    'parse.c',
    'config.c',
]

config_lib = static_library('spaceshot-config', [
    regular_sources,
    config_generated[0],
    config_generated[1],
])

config_dep = declare_dependency(sources: [config_generated[1]], link_with: config_lib)
