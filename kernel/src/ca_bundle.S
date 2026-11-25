.section .rodata.nexus_ca_bundle,"a",@progbits
.balign 16
.global nexus_builtin_ca_bundle_start
.type nexus_builtin_ca_bundle_start,@object
nexus_builtin_ca_bundle_start:
.incbin "../build/deps/ca-bundle/cacert.pem"
.byte 0
.global nexus_builtin_ca_bundle_end
nexus_builtin_ca_bundle_end:
.size nexus_builtin_ca_bundle_start, nexus_builtin_ca_bundle_end-nexus_builtin_ca_bundle_start
.section .note.GNU-stack,"",@progbits
