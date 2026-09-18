# v28 original build fix

The original-layout v28 build previously failed during `first_link` with a
1432-byte SRAM overflow. The overflow was in the generated allsyms table, not
the application data model: the original `mkallsyms.py` emitted writable
`g_nallsyms` and `g_allsyms` objects, placing them in SRAM.

Only the verified const-allsyms change was applied to the original copy. Its
original linker layout remains unchanged (flash origin `0x12010000` and the
original flash length); no mailbox reservation or bootfix linker assertions
were added. Generated allsyms contains `extern const` and `const`
declarations.

ARM/allsyms now completes:

- build_id: `rank1_v28_original_20260917`
- ELF SHA-256: `7896f1e05038cb49b71585aa0594051036244274d7493b65e7537c66492bbab6`
- BIN SHA-256: `77b8bb24c4e4dd136d0d6a406433986c06681edbb3ac2fb18c253d3ffd7bf5ea`
- System.map SHA-256: `a57817df5d5383197c3140708fa3c96bbb6e9e6254d13749df82fac3796220ef`

This remains an original-layout comparison artifact and is not marked as
compatible with the fixed boot area without a separate layout check.
