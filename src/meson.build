wl_dep = dependency('wayland-client')

# get extra protocols
wl_scanner = find_program('wayland-scanner')
wlr_protocols_dep = dependency('wlr-protocols')
wlr_protocols_dir = wlr_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
wlr_screencopy_xml = files(wlr_protocols_dir / 'unstable' / 'wlr-screencopy-unstable-v1.xml')[0]
wlr_screencopy_c = custom_target(
    'wlr-screencopy-client.c',
    output: 'wlr-screencopy-client.c',
    input: wlr_screencopy_xml,
    command: [wl_scanner, '-s', 'private-code', '@INPUT@', '@OUTPUT@'],
)
wlr_screencopy_h = custom_target(
    'wlr-screencopy-client.h',
    output: 'wlr-screencopy-client.h',
    input: wlr_screencopy_xml,
    command: [wl_scanner, '-s', 'client-header', '@INPUT@', '@OUTPUT@'],
)
wlr_screencopy_dep = declare_dependency(sources: [wlr_screencopy_c, wlr_screencopy_h])

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
    dependencies: [cairo_dep, libpng_dep, wl_dep, wlr_screencopy_dep],
)
