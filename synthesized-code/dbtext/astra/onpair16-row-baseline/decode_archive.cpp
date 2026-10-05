#include "row_adapter.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace std;
int main(int argc,char** argv) try {
    if(argc!=3) throw runtime_error("usage: decode_archive ARCHIVE OUTPUT");
    ifstream in(argv[1],ios::binary);
    if(!in) throw runtime_error("cannot open archive");
    vector<char> bytes((istreambuf_iterator<char>(in)),{});
    RowArchive archive; archive.load({reinterpret_cast<const byte*>(bytes.data()),bytes.size()});
    vector<uint8_t> output(archive.original_bytes+archive.decoder.string_boundaries.size()+16);
    size_t size=0;
    for(size_t row=0;row+1<archive.decoder.string_boundaries.size();++row) {
        size+=archive.decoder.decompress_string(row,output.data()+size);output[size++]='\n';
    }
    ofstream out(argv[2],ios::binary);out.write(reinterpret_cast<char*>(output.data()),size);
    if(!out) throw runtime_error("cannot write output");
} catch(const exception& e) { cerr<<e.what()<<endl;return 1; }
