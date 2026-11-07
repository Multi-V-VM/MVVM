#include "mvvm/reflection_codec.hpp"
#include <iostream>
#include <limits>

struct Record {
    std::map<int,std::tuple<std::string,std::vector<std::tuple<int,int,int>>>> fds;
    std::vector<std::unique_ptr<std::array<std::int64_t,2>>> frames;
    unsigned char ip[4];
    std::unordered_map<int,int> ids;
    bool ready;
};
struct View { std::span<std::uint8_t> bytes; std::uint64_t heap_offset; };
struct ViewReader {
    std::vector<char>& bytes; std::size_t at=0;
    bool read(char* p,std::size_t n) { auto v=read_view(n); if(!v) return false; std::memcpy(p,v,n); return true; }
    const char* read_view(std::size_t n) { if(n>bytes.size()-at) return nullptr; auto p=bytes.data()+at; at+=n; return p; }
};
void require(bool b) { if(!b) throw std::runtime_error("reflection regression failed"); }
int main() {
    Record a{}; a.fds[7]={"test",{{1,2,3}}};
    a.frames.emplace_back(std::make_unique<std::array<std::int64_t,2>>(std::array<std::int64_t,2>{-8,INT64_MAX}));
    a.frames.emplace_back(nullptr); a.ip[0]=127; a.ids[2]=8; a.ready=true;
    auto encoded=mvvm::codec::encode(a); auto b=mvvm::codec::decode<Record>(encoded);
    require(b.has_value()); require(b->fds==a.fds && *b->frames[0]==*a.frames[0] && !b->frames[1] && b->ip[0]==127 && b->ids==a.ids && b->ready);
    for(std::size_t n=0;n<encoded.size();++n) require(!mvvm::codec::decode<Record>(std::span(encoded).first(n)));
    auto extra=encoded; extra.push_back(std::byte{0}); require(!mvvm::codec::decode<Record>(extra));
    auto corrupt=encoded; corrupt[8]^=std::byte{1}; require(!mvvm::codec::decode<Record>(corrupt));
    auto bad=mvvm::codec::encode(std::string("abc")); std::fill(bad.begin()+16,bad.begin()+24,std::byte{255});
    require(!mvvm::codec::decode<std::string>(bad));
    require(!mvvm::codec::decode<Record>(encoded,{.bytes=8}));
    auto boolean=mvvm::codec::encode(true); boolean.back()=std::byte{2}; require(!mvvm::codec::decode<bool>(boolean));
    std::uint8_t memory[]={2,4,6,8}; auto view=mvvm::codec::serialize(View{memory,2}); ViewReader reader{view};
    auto restored=mvvm::codec::deserialize<View>(reader); require(restored.has_value());
    restored->bytes[1]=99; require(view[25]==99); require(restored->heap_offset==2);
    require(!mvvm::codec::decode<View>(std::as_bytes(std::span(view))));
    std::cout << "PASS: reflection containers, bounded corrupt input, and shared span alias\n";
}
