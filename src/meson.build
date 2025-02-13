wl_dep = dependency('wayland-client')

# get extra protocols
extra_protocols = [
    {
        'provider': 'wlr',
        'state': 'unstable',
        'name': 'wlr-screencopy',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'unstable',
        'name': 'xdg-output',
        'version': 1,
    },
]

wl_scanner = find_program('wayland-scanner')
wlr_protocols_dep = dependency('wlr-protocols')
wlr_protocols_dir = wlr_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

extra_protocol_deps = []
foreach protocol : extra_protocols
    provider = protocol['provider']
    state = protocol['state']
    name = protocol['name']
    version = protocol['version']
    if provider == 'wlr'
        xml_path = wlr_protocols_dir / state / f'@name@-@state@-v@version@.xml'
    elif provider == 'wp'
        xml_path = wayland_protocols_dir / state / name / f'@name@-@state@-v@version@.xml'
    endif

    xml_file = files(xml_path)[0]
    source_c = custom_target(
        f'@name@-client.c',
        output: f'@name@-client.c',
        input: xml_file,
        command: [wl_scanner, '-s', 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    source_h = custom_target(
        f'@name@-client.h',
        output: f'@name@-client.h',
        input: xml_file,
        command: [wl_scanner, '-s', 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    protocol_dep = declare_dependency(sources: [source_c, source_h])
    extra_protocol_deps += protocol_dep
endforeach

sources = [
    'args.c',
    'image.c',
    'main.c',
    'wayland/globals.c',
    'wayland/screenshot.c',
    'wayland/shared-memory.c',
    'wayland/surface.c',
]

cairo_dep = dependency('cairo')
libpng_dep = dependency('libpng')

executable(
    'spaceshot',
    sources,
    dependencies: [cairo_dep, libpng_dep, wl_dep, extra_protocol_deps],
)
