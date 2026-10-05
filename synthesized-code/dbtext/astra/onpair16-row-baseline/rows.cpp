#define main fsst_paper_main
#include "filtertest.cpp"
#undef main
#include "row_adapter.hpp"
#include "compressor/onpair_advanced/OnPairAdvancedCompressor.hpp"
#include <numeric>
using Encoder=sgtt::compressor::OnPairAdvancedCompressor<sgtt::compressor::onpair::MaxSymbolLength::SIXTEEN>;
class RowRunner: public CompressionRunner {
    RowArchive decoded;
    unsigned column=0;
public:
    uint64_t compressCorpus(const vector<string>& data,unsigned long& bare,double& bulk,double& compression,bool verbose) override {
        const auto start=chrono::steady_clock::now();
        vector<string_view> strings; size_t rawbytes=0;
        for(auto& s:data){strings.emplace_back(s);rawbytes+=s.size();}
        vector<byte> payload;
        Encoder encoder{sgtt::compressor::OnPairConfig{true,true}};
        encoder.prepare(rawbytes);
        encoder.compress(strings,payload);
        // Materialize a provisional single-row view to determine token lengths.
        auto put64=[](vector<byte>& out,uint64_t v){size_t n=out.size();out.resize(n+8);memcpy(out.data()+n,&v,8);};
        uint64_t begin; memcpy(&begin,payload.data(),8);
        if(begin>payload.size()-8) throw runtime_error("invalid encoder output");
        uint64_t tokenCount=(payload.size()-8-begin)/2;
        vector<byte> provisional;
        const char magic[]="OPROW001";
        provisional.insert(provisional.end(),reinterpret_cast<const byte*>(magic),reinterpret_cast<const byte*>(magic)+8);
        put64(provisional,1);put64(provisional,payload.size());put64(provisional,0);put64(provisional,tokenCount);
        provisional.insert(provisional.end(),payload.begin(),payload.end());
        RowArchive bridge; bridge.load(provisional);
        vector<byte> archive(provisional.begin(),provisional.begin()+8);
        put64(archive,data.size());put64(archive,payload.size());put64(archive,0);
        size_t token=0;
        for(auto& row:data) {
            size_t len=0;
            while(len<row.size()) {
                if(token==tokenCount) throw runtime_error("missing row tokens");
                auto id=bridge.decoder.compressed_data[token++];
                len+=bridge.decoder.token_boundaries[id+1]-bridge.decoder.token_boundaries[id];
            }
            if(len!=row.size()) throw runtime_error("token crosses row boundary");
            put64(archive,token);
        }
        if(token!=tokenCount) throw runtime_error("extra row tokens");
        archive.insert(archive.end(),payload.begin(),payload.end());
        compression=bulk=chrono::duration<double>(chrono::steady_clock::now()-start).count();
        const auto setup=chrono::steady_clock::now();
        decoded.load(archive); // Independent cold load; nothing retained from encoder/training.
        const auto setupSeconds=chrono::duration<double>(chrono::steady_clock::now()-setup).count();
        // Verify every row outside timing.
        vector<uint8_t> check;
        for(size_t i=0;i<data.size();++i) {
            check.resize(data[i].size()+16);
            auto n=decoded.decoder.decompress_string(i,check.data());
            if(n!=data[i].size() || memcmp(check.data(),data[i].data(),n)) throw runtime_error("full row mismatch");
        }
        if(const char* folder=getenv("AUDIT_EXPORT_DIR")) {
            ofstream out(string(folder)+"/"+to_string(column)+".bin",ios::binary);
            out.write(reinterpret_cast<const char*>(archive.data()),archive.size());
            if(!out) throw runtime_error("archive export failed");
        }
        if(const char* path=getenv("ADAPTER_SETUP_TSV")) {
            ofstream out(path,ios::app);out.precision(17);
            out<<column<<'\t'<<setupSeconds<<'\t'<<compression<<'\t'<<decoded.decoder.dictionary.size()<<'\t'<<decoded.decoder.token_boundaries.size()*4<<'\t'<<decoded.decoder.string_boundaries.size()*8<<'\t'<<archive.size()<<'\n';
        }
        ++column; bare=archive.size();
        if(verbose) cout<<"# bridge encoding time: "<<compression<<"; cold setup: "<<setupSeconds<<endl;
        return archive.size();
    }
    uint64_t decompressRows(vector<char>& target,const vector<unsigned>& lines) override {
        auto* writer=reinterpret_cast<uint8_t*>(target.data());
        for(auto row:lines) {
            writer+=decoded.decoder.decompress_string(row,writer);
            *writer++='\n';
        }
        return writer-reinterpret_cast<uint8_t*>(target.data());
    }
};
int main(int argc,const char* argv[]) try {
    if(argc<2) throw runtime_error("usage: rows COLUMN...");
    RowRunner runner;
    return !doTest(runner,vector<string>(argv+1,argv+argc),true).first;
} catch(const exception& e) { cerr<<e.what()<<endl;return 1; }
