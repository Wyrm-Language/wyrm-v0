embed_stage2 = custom_target('embed-stage2',
    output: embed_outputs,
    command: [embed_stage, '--mode', 'self', '--wyrm', wyrm_stage1,
              '--src', meson.project_source_root(), '--out', '@OUTDIR@'],
    depend_files: embed_inputs,
    build_by_default: false,
    console: true,
)
