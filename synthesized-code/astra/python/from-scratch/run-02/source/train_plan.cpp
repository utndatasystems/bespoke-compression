// Optional offline training entry point. This reproduces the fitted parse from
// the original input; the source build itself uses the retained fitted model.
#include "lzparse.h"
#include "lexical.h"
#include "grammar_indent_best.h"
#include <fstream>
int main(int argc,char**argv){
 if(argc!=3)return 2;
 std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
 if(!input)return 2;size_t n=size_t(input.tellg());input.seekg(0);
 std::vector<uint8_t> raw(n);if(!input.read((char*)raw.data(),n))return 2;
 auto indentation=indbest::encode(raw.data(),raw.size());
 auto words=lexical::encode(indentation.body.data(),indentation.body.size(),1,1024,123);
 auto seq=parse_lz(words.transformed.data(),words.transformed.size());
 static_assert(sizeof(LZSeq)==12,"The training file uses three little-endian uint32 fields.");
 std::ofstream output(argv[2],std::ios::binary);
 output.write((const char*)seq.data(),seq.size()*sizeof(LZSeq));
 return output?0:2;
}
