# SiFli SF32LB52 HCI command-completion loss evidence

The accompanying offline evidence archive is:

`artifacts/diagnostics/sifli_sf32lb52_hci_cc_loss_repro_20260902_r2.tar.gz`

Its SHA-256 is:

`801a1fa8ae20ed256fc3bf154bc614a5d9222b23cee350759aa3ecdc7295724a`

The archive manifest covers 20 payload files. All payload hashes passed `sha256sum -c`, the archive passed `tar -tzf`, and a clean temporary extraction passed the same SHA-256 verification again.

## Evidence summary

The target is SF32LB52 silicon `REVID=0x03` and runs the legacy RAM LCPU image plus legacy patch path, not the Rev-B ROM-patch controller path.

During the controlled two-cycle reproduction, advertising recovery after the first disconnection succeeds. During recovery after the second disconnection, the LCPU consumes the HCI `0x2036` (LE Set Extended Advertising Parameters) command from the shared TX ring, but publishes no corresponding Command Complete or Command Status to the shared RX ring. RX producer/consumer mirrors and Adapter mailbox, worker, drain, byte, callback, complete-H4, and forward counters show no progress before the Host's genuine CC/CS wait times out.

This material is reproduction evidence for a failure within the opaque SiFli controller/LCPU binary or ROM boundary. It is not a formal functional fix, and it does not prove that the `0x2036` handler alone is necessarily defective.

The serial material in the archive is a user-provided transcription manually uploaded from a Codex conversation. It is not an original byte-for-byte UART capture exported by serial terminal software; that original capture remains an outstanding evidence item.

This evidence-only commit does not modify `vendor/sifli`, `apps`, `nuttx`, or any public `open-vela` repository. The package has not been uploaded to SiFli, emailed to SiFli, or filed as a vendor issue.
