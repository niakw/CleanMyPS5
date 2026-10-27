#include "../native/cache_delta.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace cmp5_cache_delta;
static Cmp5CacheTop make(const char*name,unsigned kind,
                         std::uint64_t bytes,unsigned files){
 Cmp5CacheTop c{};
 std::strcpy(c.app_label,name);
 c.kind=kind;c.nominal_bytes=bytes;c.files=files;
 c.metadata_complete=1u;c.metadata_nodes=files+1u;
 c.metadata_xor=bytes^((std::uint64_t)files<<32u);
 c.metadata_sum=bytes+files;
 return c;
}
int main(){
 Cmp5HomebrewStats a{},b{};
 assert(!complete(a));
 a.data_root_state=b.data_root_state=1u;
 a.cache_top_count=b.cache_top_count=2u;
 a.cache_top[0]=make("ToolA",1u,2048u,2u);
 a.cache_top[1]=make("ToolB",3u,1u,1u);
 b.cache_top[0]=make("ToolB",3u,1u,1u);
 b.cache_top[1]=make("ToolA",1u,4096u,3u);
 auto r=compare(a,b,false);
 assert(r.state==State::no_baseline&&!r.idle_verified);
 r=compare(a,b,true);
 assert(r.state==State::compared&&r.compared==2u);
 assert(r.changed==1u&&r.unchanged_observed==1u);
 assert(r.entries[0].result==Kind::unchanged_observed);
 assert(r.entries[1].result==Kind::changed);
 assert(r.entries[1].old_bytes==2048u&&r.entries[1].new_bytes==4096u);
 assert(!r.idle_verified&&!r.deletion_allowed);
 b.cache_top[1].nominal_bytes=2048u;b.cache_top[1].files=2u;
 r=compare(a,b,true);
 assert(r.changed==1u&&r.unchanged_observed==1u);
 // Same totals but different metadata: not a stable directory.
 b.cache_top[1].metadata_xor=a.cache_top[0].metadata_xor;
 b.cache_top[1].metadata_sum=a.cache_top[0].metadata_sum;
 b.cache_top[1].metadata_nodes=a.cache_top[0].metadata_nodes;
 r=compare(a,b,true);
 assert(r.changed==0u&&r.unchanged_observed==2u);
 b.cache_top[1].metadata_xor^=1u;
 r=compare(a,b,true);
 assert(r.changed==1u&&r.unchanged_observed==1u);
 b.cache_top[1].metadata_complete=0u;
 r=compare(a,b,true);
 assert(r.incomparable==1u&&r.compared==1u);
 b.cache_top[1].metadata_complete=1u;
 assert(!r.idle_verified&&!r.deletion_allowed);
 b.cache_top[0]=make("Missing",3u,1u,1u);
 r=compare(a,b,true);
 assert(r.compared==1u&&r.incomparable==1u);
 b.cache_top[1].partial=1u;
 r=compare(a,b,true);
 assert(r.state==State::incomplete&&r.compared==0u);
 b.cache_top[1].partial=0u;
 b.cache_like_partial=1u;
 assert(compare(a,b,true).state==State::incomplete);
 b.cache_like_partial=0u;
 b.data_root_state=3u;
 assert(compare(a,b,true).state==State::incomplete);
 b.data_root_state=1u;
 b.cache_top[1]=make("Missing",3u,1u,1u);
 assert(compare(a,b,true).incomparable==2u); // duplicate sanitised label
 b.cache_top[1]=make("ToolA",1u,2u,1u);
 a.cache_top[1]=make("ToolA",1u,3u,1u);
 assert(compare(a,b,true).incomparable==2u); // non-unique candidate key
 a.cache_top[1]=make("ToolB",3u,1u,1u);
 b.cache_top[0]=make("ToolB",3u,1u,1u);
 b.cache_top[1]=make("ToolA",1u,5u,1u);
 std::memset(b.cache_top[1].app_label,'X',47u);
 b.cache_top[1].app_label[47]=0;
 assert(compare(a,b,true).state==State::incomplete);
 puts("Local cache two-scan deltas: changed != disposable, stable != idle: PASS");
}
