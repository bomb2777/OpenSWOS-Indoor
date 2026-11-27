# Monocypher in OpenSWOS-Indoor

- Upstream: https://github.com/LoupVaillant/Monocypher, tag **4.0.2**, files `src/monocypher.c`,
  `src/monocypher.h` and `LICENCE.md`, unchanged (2026-11-27).
  sha256 monocypher.c `02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d`,
  monocypher.h `fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3`.
- Licence: dual, 2-clause BSD or CC0 (LICENCE.md) -- either is compatible with the GPL.
- Used for: the sealed messages between the game and the ranking desk of the fork's server
  (X25519 + BLAKE2b + XChaCha20-Poly1305; `include/rank_proto.h`, `src/rank_seal.c`). Compiled
  into the game by `src/monocypher_impl.c`, into the server by third_party/matchmaker's
  CMakeLists.txt.
