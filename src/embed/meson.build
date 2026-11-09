# The images the wyrm binary embeds are build output, never committed (project
# design/plans/I-generated-images.md).
#
#   stage1: wyrm_bootstrap, a preexisting wyrm or wypoc, generates them from the .wy
#           sources (or -Dembed_prebuilt supplies a generated tree).
#   stage2: wyrm-stage1, built from stage1's images, generates them again.
#
# libwyrmhost embeds the stage -Dbootstrap_stages picks. Each stage has its own
# directory because meson outputs are unique per directory. Each target declares only
# the two root files: the per-module files are #included by the unity file, so the
# compiler's depfile tracks them. Hand-written natives stay here, committed.
embed_inc = include_directories('.')
embed_sources = []
embed_final_dir = ''

if get_option('hosted')
    fs = import('fs')

    embed_stage = find_program('../../scripts/embed_stage.py')
    embed_rows = fs.read('modules.txt').strip().split('\n')
    embed_inputs = files(embed_rows) + files('modules.txt', '../../wy/wyrm/tools/embed_build.wy',
                                             '../../scripts/embed_stage.py')
    embed_outputs = ['embedded_images.c', 'wyrm_builtins_table.c']
    # std::io's POSIX natives (open/read/write/lseek/... on real descriptors).
    embed_native = files('std/io_native.c')

    bootstrap_path = get_option('wyrm_bootstrap')
    prebuilt_path = get_option('embed_prebuilt')
    if bootstrap_path != '' and prebuilt_path != ''
        error('Set one of -Dwyrm_bootstrap and -Dembed_prebuilt, not both.')
    endif
    # A release tarball carries its images here (see the dist script below).
    if bootstrap_path == '' and prebuilt_path == '' and fs.exists('prebuilt/embedded_images.c')
        prebuilt_path = 'src/embed/prebuilt'
    endif
    # With nothing named, the wyrm on PATH. It's usually wypoc, which generates the images
    # from source: no prebuilt binary is needed, but it takes minutes.
    if bootstrap_path == '' and prebuilt_path == '' and not find_program('wyrm', required: false).found()
        error('The embedded images are generated, and this tree has nothing to generate them with. '
              + 'Put a wypoc `wyrm` on PATH, or set -Dwyrm_bootstrap=/path/to/wyrm (a wypoc, or a C wyrm: '
              + '`ninja wyrm_bootstrap` in another tree builds one, and takes seconds where wypoc takes '
              + 'minutes), or -Dembed_prebuilt=DIR with a generated tree (a release tarball carries one).')
    endif

    if prebuilt_path != ''
        embed_stage1_dir = meson.project_source_root() / prebuilt_path
        if not fs.exists(embed_stage1_dir / 'embedded_images.c')
            error('-Dembed_prebuilt=' + prebuilt_path + ' has no embedded_images.c')
        endif
        embed_stage1_sources = files(embed_stage1_dir / 'embedded_images.c',
                                     embed_stage1_dir / 'wyrm_builtins_table.c')
        embed_stage1_depends = []
    else
        if bootstrap_path == ''
            bootstrap_path = 'wyrm'
        endif
        # A bare name comes from PATH; anything else is a path from the source root.
        if bootstrap_path.contains('/')
            wyrm_bootstrap = find_program(meson.project_source_root() / bootstrap_path)
        else
            wyrm_bootstrap = find_program(bootstrap_path)
        endif
        bootstrap_probe = run_command(embed_stage, '--probe', '--wyrm', wyrm_bootstrap,
                                      '--min', files('../../scripts/wyrm_bootstrap_min.txt'), check: false)
        if bootstrap_probe.returncode() != 0
            error('-Dwyrm_bootstrap=' + bootstrap_path + ': ' + bootstrap_probe.stderr().strip())
        endif
        message('wyrm_bootstrap ' + wyrm_bootstrap.full_path() + ': image version ' + bootstrap_probe.stdout().strip())

        subdir('stage1')
        embed_stage1_dir = meson.current_build_dir() / 'stage1'
        embed_stage1_sources = [embed_stage1[0], embed_stage1[1]]
        embed_stage1_depends = [embed_stage1]
        alias_target('stage1', embed_stage1)
    endif

    # Sources for a binary that needs nothing from this build tree at run time.
    embed_standalone_files = files('../wyrm/main.c', '../host/host.c', '../host/repl.c')
    embed_standalone_inc = [cwyrm_lib_inc, embed_inc, include_directories('../host')] + extra_inc

    # Always optimized: on a debug tree this is the compiler that runs the compiler,
    # and its speed sets the cost of stage 2.
    wyrm_stage1 = executable('wyrm-stage1',
        embed_standalone_files + cwyrm_sources + extra_sources + embed_native + embed_stage1_sources,
        include_directories: embed_standalone_inc,
        dependencies: extra_deps + [libm_dep],
        override_options: ['optimization=2', 'debug=false'],
        build_by_default: false,
    )
    subdir('stage2')
    alias_target('stage2', embed_stage2)
    run_target('embed-check',
        depends: embed_stage1_depends + [embed_stage2],
        command: [embed_stage, '--compare', embed_stage1_dir, meson.current_build_dir() / 'stage2'])

    if get_option('bootstrap_stages') == '2'
        embed_final_sources = [embed_stage2[0], embed_stage2[1]]
        embed_final_dir = meson.current_build_dir() / 'stage2'
        embed_final_target = 'stage2'
    else
        embed_final_sources = embed_stage1_sources
        embed_final_dir = embed_stage1_dir
        embed_final_target = prebuilt_path == '' ? 'stage1' : ''
    endif
    embed_sources = embed_final_sources + embed_native

    if prebuilt_path == ''
        run_target('embed-regen',
            command: [embed_stage, '--regen', meson.project_build_root(),
                      meson.current_build_dir() / 'stage1', meson.current_build_dir() / 'stage2'])
    endif

    # A wyrm you can keep once this build tree is gone. Linked statically where it can
    # be; with GLib enabled it links the system's libraries, still not this tree's.
    bootstrap_link_args = []
    if not glib_dep.found() and cc_compiler.links('int main(void) { return 0; }', args: ['-static'],
                                                  name: 'static linking')
        bootstrap_link_args = ['-static']
    endif
    wyrm_bootstrap_exe = executable('wyrm_bootstrap',
        embed_standalone_files + cwyrm_sources + extra_sources + embed_sources,
        include_directories: embed_standalone_inc,
        dependencies: extra_deps + [libm_dep],
        link_args: bootstrap_link_args,
        override_options: ['optimization=2', 'debug=false'],
        build_by_default: false,
    )
    alias_target('wyrm_bootstrap', wyrm_bootstrap_exe)

    # A tarball builds with only a C compiler: it carries a generated tree.
    if embed_final_target != ''
        meson.add_dist_script(embed_stage, '--dist', embed_final_dir, embed_final_target)
    else
        meson.add_dist_script(embed_stage, '--dist', embed_final_dir)
    endif
endif
