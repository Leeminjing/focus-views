#include "runtime/store.hpp"
#include "runtime/query-stream.hpp"
int wmain(int argc,wchar_t** argv){using namespace r5003;try{
    const r3::Args args(argc,argv);const auto app=std::filesystem::path(args.get(L"--fixture"));const auto file=std::filesystem::path(args.get(L"--snapshot"));const auto out=std::filesystem::path(args.get(L"--output"));
    demand(!std::filesystem::exists(out),"new codec evidence directory");std::filesystem::create_directories(out);
    Log log(out/"codec.jsonl");const auto identity=native::digest(native::read_file(app));const auto original=read(file);const auto s=decode(unpack(Kind::snapshot,original),identity);unsigned checks=0;
    auto okay=[&](const char* name,bool pass){log.emit("check","\"name\":"+json(name)+",\"passed\":"+(pass?"true":"false"));if(!pass)throw Error("codec_test.assertion",ERROR_INVALID_DATA,name);++checks;};
    okay("raw-roundtrip",envelope(Kind::snapshot,encode(s))==original);
    {auto invalid=s;invalid.meta.abi_version=77;publish(out/"invalid-abi.fvs",Kind::snapshot,encode(invalid));}
    auto reject=[&](const char* name,const char* api,const std::function<void()>& action){try{action();throw Error("expected_rejection_missing",ERROR_INVALID_STATE);}catch(const Error& e){log.emit("rejection","\"name\":"+json(name)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));okay(name,e.api==api);}};
    reject("tamper","snapshot.integrity",[&]{auto b=original;b[b.size()/2]^=1;unpack(Kind::snapshot,b);});
    reject("truncation","snapshot.blob_length",[&]{auto b=original;b.resize(b.size()/2);unpack(Kind::snapshot,b);});
    reject("unknown-format","snapshot.format_version",[&]{auto b=original;b[8]=77;unpack(Kind::snapshot,b);});
    reject("unknown-runtime-abi","snapshot.runtime_abi",[&]{auto x=s;x.meta.abi_version=77;decode(encode(x),identity);});
    reject("bad-layout","snapshot.execution_layout",[&]{auto x=s;x.layout.frames=8;decode(encode(x),identity);});
    reject("bad-region-length","snapshot.region_length",[&]{auto x=s;x.memory.pop_back();decode(encode(x),identity);});
    reject("tiny-region-reference-underflow","snapshot.region_length",[&]{auto x=s;x.meta.data_bytes=1;x.memory.resize(1);decode(encode(x),identity);});
    reject("cookie-span-outside-image","snapshot.image_addresses",[&]{auto x=s;x.meta.cookie_address=x.image.base+x.image.bytes.size()-1;decode(encode(x),identity);});
    reject("truncated-PE-header","snapshot.PE_headers",[&]{auto x=s;const LONG bad=static_cast<LONG>(x.image.bytes.size()-1);memcpy(x.image.bytes.data()+offsetof(IMAGE_DOS_HEADER,e_lfanew),&bad,sizeof(bad));decode(encode(x),identity);});
    reject("bad-reference","snapshot.reference_bounds",[&]{auto x=s;x.meta.refs[0].address=1;x.meta.required_refs[0]=x.meta.refs[0];decode(encode(x),identity);});
    reject("wrong-target","snapshot.target_image",[&]{auto wrong=identity;wrong[0]^=1;decode(unpack(Kind::snapshot,original),wrong);});
    reject("interrupted-write","snapshot.interrupted",[&]{save(out/"interrupted.fvs",s,true);});okay("no-partial-publication",!std::filesystem::exists(out/"interrupted.fvs")&&std::filesystem::is_empty(out)==false);
    for(const auto& p:std::filesystem::directory_iterator(out))okay("staging-removed",p.path().extension()!=L".partial");
    save(out/"roundtrip.fvs",s);okay("published-load",encode(load(out/"roundtrip.fvs",identity))==encode(s));
    reject("immutable-version","snapshot.version_exists",[&]{save(out/"roundtrip.fvs",s);});okay("existing-version-unchanged",read(out/"roundtrip.fvs")==original);
    {auto channel_owner=std::make_unique<r304::Channel>();auto& channel=*channel_owner;channel.pid=GetCurrentProcessId();channel.tid=GetCurrentThreadId();channel.token=GetTickCount64();r5003query::Consumer consumer(channel);r5003query::Producer producer(channel);
        for(unsigned i=0;i<r304::query_capacity;++i){r304::Query q;q.sequence=i;producer.write(channel,q);}
        reject("query-stream-backpressure","query_stream.backpressure",[&]{producer.write(channel,{});});
        unsigned consumed=0;bool ordered=true;for(;consumed<r304::query_capacity/2;++consumed)ordered=ordered&&consumer.at(consumed).sequence==consumed;okay("query-FIFO-before-wrap",ordered);consumer.acknowledge(consumed);
        for(unsigned i=0;i<r304::query_capacity/2;++i){r304::Query q;q.sequence=r304::query_capacity+i;producer.write(channel,q);}
        for(;consumed<consumer.count();++consumed)if(consumer.at(consumed).sequence!=consumed)throw Error("query_stream.FIFO_loss",ERROR_INVALID_DATA);consumer.acknowledge(consumed);
        okay("query-stream-wrap-no-loss",consumed==r304::query_capacity+r304::query_capacity/2);
    }
    log.emit("complete","\"passed\":true,\"checks\":"+std::to_string(checks));return 0;
}catch(const std::exception& e){std::cerr<<"CODEC_FAILURE "<<e.what()<<'\n';return 1;}}
