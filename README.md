# Huffman file compressor (C11)

Compress and restore arbitrary binary files using a deterministic Huffman tree. `fixtures/message.txt` is synthetic repeated text. The restored example is in `examples/restored.txt` and matches the input byte for byte.

```sh
cc -std=c11 -O2 -Wall -Wextra -pedantic main.c -o huffman
./huffman compress fixtures/message.txt message.huf
./huffman decompress message.huf restored.txt
cmp fixtures/message.txt restored.txt
cc -std=c11 -O2 -Wall -Wextra -pedantic -Wno-unused-function tests.c -o tests
./tests
```

The archive begins with `HUF1`, followed by 256 little-endian 64-bit symbol frequencies, then most-significant-bit-first coded bytes with zero padding. Ties in the tree use the smallest symbol for reproducible archives. The 2,052-byte header makes tiny files larger. The demo shrinks 8,900 bytes to 5,840 bytes. Empty and single-symbol files are supported. Input and declared decoded size are limited to 16 MiB. Truncated headers/payloads, invalid padding, unexpected trailing data, and decoded frequency mismatches fail nonzero. Input must be seekable. Output paths must be new files; an existing output is rejected, and a partial output is removed if final copying fails.
