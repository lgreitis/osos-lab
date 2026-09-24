# Golden fixtures

`fast-aes.json` contains AES-CBC ciphertext and SHA-256 digests of concatenated
payloads assembled independently with wInd3x's Go assembler (`pkg/uasm`).

- Ciphertext: Go `crypto/aes` with CBC, key `offline-test-key`, IV `[0x71; 16]`,
  plaintext byte `i = (i * 37 + i / 19) mod 256`, and no extra padding.
- Payload digest input order is defined by `fast_payloads_match_go_for_every_chunk_size`
  and the NOR fixture test in `../tests.rs`.
- Other golden instruction bytes and the BootROM payload digest live in `../tests.rs`.

The original Go generator is not included. To regenerate payload references,
independently assemble the sequences from `../payload.rs` with wInd3x and hash
them in the test's order; do not derive expected values from the Rust assembler.
