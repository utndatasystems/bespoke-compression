#include "fsst.h"
#include <sys/mman.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<unsigned char> input((std::istreambuf_iterator<char>(file)), {});
  if (input.empty()) return 3;
  const size_t guard = 1 << 20, mapped = guard + input.size() + 4096;
  auto base = static_cast<unsigned char*>(mmap(nullptr, mapped, PROT_READ | PROT_WRITE,
                                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (base == MAP_FAILED || mprotect(base, guard, PROT_NONE)) return 4;
  unsigned char* raw = base + guard;
  memcpy(raw, input.data(), input.size());
  unsigned long length = input.size(), compressedLength = 0;
  unsigned char* compressedPointer = nullptr;
  auto encoder = fsst_create(1, &length, &raw, 0);
  if (!encoder) return 5;
  std::vector<unsigned char> compressed(2 * length + 16), output(length + 64), table(FSST_MAXHEADER);
  if (fsst_compress(encoder, 1, &length, &raw, compressed.size(), compressed.data(),
                    &compressedLength, &compressedPointer) != 1) return 6;
  auto tableLength = fsst_export(encoder, table.data());
  fsst_decoder_t decoder;
  if (fsst_import(&decoder, table.data()) != tableLength) return 7;
  auto decoded = fsst_decompress(&decoder, compressedLength, compressedPointer,
                                 output.size(), output.data());
  if (decoded != length || memcmp(output.data(), raw, length)) return 8;
  fsst_destroy(encoder);
  munmap(base, mapped);
  std::cout << "byte_exact " << decoded << " payload " << compressedLength
            << " table " << tableLength << '\n';
}
