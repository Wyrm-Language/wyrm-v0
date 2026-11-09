embed_stage1 = custom_target('embed-stage1',
    output: embed_outputs,
    command: [embed_stage, '--mode', 'bootstrap', '--wyrm', wyrm_bootstrap,
              '--src', meson.project_source_root(), '--out', '@OUTDIR@'],
    depend_files: embed_inputs + files(wyrm_bootstrap.full_path()),
    console: true,
)
