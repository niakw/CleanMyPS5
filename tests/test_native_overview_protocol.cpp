// Strict, host-only regression tests for the native overview JSON decoder.
#include "native/overview_protocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { if(!(x)) { \
 std::fprintf(stderr,"overview protocol check failed: %s at %d\n",#x,__LINE__); \
 std::exit(1); } } while(0)
using cmp5_native::Overview;
static const std::string valid=
 R"({"schemaVersion":1,"mode":"metadata-only","cleanupValidated":false,)"
 R"("scope":"bounded-known-roots","files":3,"nominalBytes":50000000,)"
 R"("partial":true,"categories":{"media":2,"logs":1,"cache":0,"residuals":0},)"
 R"("review":{"largeMedia":1,"oldMedia":2,"oldLargeMedia":1,)"
 R"("largeMediaNominalBytes":40000000,"validatedForDeletion":false},)"
 R"("coverage":{"complete":2,"partial":1},)"
  R"("galleryCoverage":{"photo":"complete","video":"unavailable"},)"
  R"("note":"Nominal only"})";
static bool accepted(const std::string&value){
 Overview out{};
 return cmp5_native::parse_overview(value.data(),value.size(),&out);
}
static void reject(const std::string&value){
 Overview out{};out.files=12345;
 CHECK(!cmp5_native::parse_overview(value.data(),value.size(),&out));
 CHECK(out.files==12345); // Fail closed without publishing partial fields.
}
static std::string replace_once(std::string value,const std::string&from,
 const std::string&to){
 const auto at=value.find(from);
 CHECK(at!=std::string::npos);
 value.replace(at,from.size(),to);
 return value;
}
int main(){
 CHECK(accepted(valid));
 Overview out{};
 CHECK(cmp5_native::parse_overview(valid.data(),valid.size(),&out));
 CHECK(out.files==3&&out.media==2&&out.logs==1&&out.cache==0);
 CHECK(out.large==1&&out.old==2&&out.old_large==1);
 CHECK(out.bytes==50000000&&out.large_bytes==40000000&&out.partial);
  CHECK(out.gallery_photo==1u&&out.gallery_video==4u);
 CHECK(accepted(replace_once(valid,R"("partial":true)",R"("partial":false)")));
 CHECK(accepted(replace_once(valid,R"("note":"Nominal only")",
   R"("note":"Escaped \"files\":8888 stays inside the string")")));
 CHECK(accepted(replace_once(valid,R"("coverage":{"complete":2,"partial":1})",
   R"("coverage":{"files":999,"categories":{"media":999},"partial":false})")));
 reject(replace_once(valid,R"("schemaVersion":1)",R"("schemaVersion":2)"));
 reject(replace_once(valid,R"("mode":"metadata-only")",R"("mode":"writable")"));
 reject(replace_once(valid,R"("mode":"metadata-only",)",""));
 reject(replace_once(valid,R"("mode":"metadata-only")",
   R"("mode":"metadata-only","mode":"metadata-only")"));
 reject(replace_once(valid,R"("scope":"bounded-known-roots")",
   R"("scope":"other-roots")"));
 reject(replace_once(valid,R"("cleanupValidated":false)",
   R"("cleanupValidated":true)"));
 reject(replace_once(valid,R"("validatedForDeletion":false)",
   R"("validatedForDeletion":true)"));
 reject(replace_once(valid,R"("galleryCoverage":{"photo":"complete","video":"unavailable"})",
    R"("galleryCoverage":{"photo":"complete","video":"all-access"})"));
  reject(replace_once(valid,R"("galleryCoverage":{"photo":"complete","video":"unavailable"})",
    R"("galleryCoverage":{"photo":"complete","photo":"partial","video":"unavailable"})"));
  reject(replace_once(valid,R"("galleryCoverage":{"photo":"complete","video":"unavailable"})",
    R"("galleryCoverage":{"photo":"complete"})"));
  reject(replace_once(valid,R"("partial":true)",R"("partial":"true")"));
 reject(replace_once(valid,R"("partial":true)",R"("partial":true,"partial":false)"));
 reject(replace_once(valid,R"("files":3)",R"("files":3,"files":3)"));
 reject(replace_once(valid,R"("media":2)",R"("media":2,"media":2)"));
 reject(replace_once(valid,R"("largeMedia":1)",R"("largeMedia":1,"largeMedia":1)"));
 reject(replace_once(valid,R"("files":3)",R"("files":4294967296)"));
 reject(replace_once(valid,R"("files":3)",R"("files":03)"));
 reject(replace_once(valid,R"("files":3)",R"("files":-1)"));
 reject(replace_once(valid,R"("nominalBytes":50000000)",
   R"("nominalBytes":18446744073709551616)"));
 reject(replace_once(valid,R"("files":3)",R"("files":1)"));
 reject(replace_once(valid,R"("largeMedia":1)",R"("largeMedia":3)"));
 reject(replace_once(valid,R"("oldLargeMedia":1)",R"("oldLargeMedia":2)"));
 reject(replace_once(valid,R"("largeMediaNominalBytes":40000000)",
   R"("largeMediaNominalBytes":60000000)"));
 reject(replace_once(valid,R"("categories":{"media":2,"logs":1,"cache":0,"residuals":0})",
   R"("categories":{"media":2,"cache":0})"));
 reject(replace_once(valid,R"("review":{"largeMedia":1,"oldMedia":2,"oldLargeMedia":1,)",
   R"("review":{"largeMedia":1,"oldLargeMedia":1,)"));
 reject(replace_once(valid,R"("note":"Nominal only")",
   R"("note":"bad \q escape")"));
 reject(replace_once(valid,R"("note":"Nominal only")",
   R"("note":"truncated \u00HH escape")"));
 reject(valid+" trailing");
 reject(valid.substr(0,valid.size()-1));
 CHECK(!cmp5_native::parse_overview(nullptr,0,&out));
 CHECK(!cmp5_native::parse_overview(valid.data(),valid.size(),nullptr));
 CHECK(!cmp5_native::parse_overview(valid.data(),valid.size()-1,&out));
 CHECK(!cmp5_native::parse_overview(valid.data(),4097,&out));
 const std::string preflight=
  R"({"schemaVersion":1,"mode":"read-only","scope":"known-paths-only","entries":[)"
  R"({"path":"/data","status":"accessible","reason":"mount-unverified","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/deny1","status":"denied","reason":"permission-denied","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/deny2","status":"denied","reason":"permission-denied","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/absent1","status":"absent","reason":"not-found","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/absent2","status":"absent","reason":"not-found","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/absent3","status":"absent","reason":"not-found","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/absent4","status":"absent","reason":"not-found","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false},)"
  R"({"path":"/alias","status":"symlink","reason":"symlink-blocked","writableHint":false,"readOnlyMount":false,"mountInfoKnown":false}],)"
  R"("summary":{"accessible":1,"denied":2,"absent":4,"other":1,"writableHints":0,"mountInfoUnknown":1}})";
 cmp5_native::Preflight permissions{};
 CHECK(cmp5_native::parse_preflight(preflight.data(),preflight.size(),&permissions));
 CHECK(permissions.accessible==1&&permissions.denied==2&&
   permissions.absent==4&&permissions.other==1&&
   permissions.mount_info_reported&&permissions.mount_info_unknown==1u);
 const std::string legacy_response=
  R"({"schemaVersion":1,"mode":"read-only","scope":"known-paths-only","entries":[)"
  R"({"path":"/data","status":"accessible","reason":"directory-opened","writableHint":false,"readOnlyMount":false}],)"
  R"("summary":{"accessible":1,"denied":0,"absent":0,"other":0,"writableHints":0}})";
 cmp5_native::Preflight legacy{};
 CHECK(cmp5_native::parse_preflight(legacy_response.data(),legacy_response.size(),&legacy));
 CHECK(!legacy.mount_info_reported);
 auto invalid_preflight=[&](const std::string&input){
  cmp5_native::Preflight original{};
  original.accessible=1234;
  CHECK(!cmp5_native::parse_preflight(input.data(),input.size(),&original));
  CHECK(original.accessible==1234);
 };
 invalid_preflight(replace_once(preflight,R"("mode":"read-only")",R"("mode":"writable")"));
 invalid_preflight(replace_once(preflight,R"("scope":"known-paths-only")",
   R"("scope":"all-roots")"));
 invalid_preflight(replace_once(preflight,R"("schemaVersion":1)",R"("schemaVersion":2)"));
 invalid_preflight(replace_once(preflight,R"("accessible":1)",R"("accessible":1,"accessible":2)"));
 invalid_preflight(replace_once(preflight,R"("accessible":1)",R"("accessible":-1)"));
 invalid_preflight(replace_once(preflight,R"("accessible":1)",R"("accessible":99999999999)"));
 invalid_preflight(replace_once(preflight,R"("accessible":1)",R"("accessible":1025)"));
 invalid_preflight(replace_once(preflight,R"("summary":{"accessible":1,"denied":2,"absent":4,"other":1,"writableHints":0,"mountInfoUnknown":1})",
   R"("summary":{"accessible":1,"denied":2,"other":1})"));
 invalid_preflight(replace_once(preflight,R"("summary":{"accessible":1,"denied":2,"absent":4,"other":1,"writableHints":0,"mountInfoUnknown":1})",
   R"("result":{"summary":{"accessible":1,"denied":2,"absent":4,"other":1}})"));
 invalid_preflight(replace_once(preflight,
   R"("mountInfoUnknown":1)",R"("mountInfoUnknown":1,"mountInfoUnknown":1)"));
 invalid_preflight(replace_once(preflight,
   R"("mountInfoUnknown":1)",R"("mountInfoUnknown":2)"));
 invalid_preflight(replace_once(preflight,
   R"("denied":2)",R"("denied":3)"));
 invalid_preflight(replace_once(preflight,
   R"("path":"/deny1")",R"("path":"../deny1")"));
 invalid_preflight(replace_once(preflight,
   R"("path":"/deny1")",R"("path":"/data/../user")"));
 invalid_preflight(replace_once(preflight,
   R"("path":"/deny1")",R"("path":"/data/./user")"));
 invalid_preflight(replace_once(preflight,
   R"("path":"/deny1")",R"("path":"/data//user")"));
 invalid_preflight(replace_once(preflight,
   R"("path":"/deny1")",R"("path":"/deny2")"));
 invalid_preflight(replace_once(preflight,
   R"("status":"denied","reason":"permission-denied")",
   R"("status":"accessible","reason":"directory-opened")"));
 invalid_preflight(replace_once(preflight,
   R"("mountInfoKnown":false)",R"("mountInfoKnown":true)"));
 invalid_preflight(replace_once(preflight,
   R"("mountInfoUnknown":1)",R"("mountInfoUnknown":0)"));
 invalid_preflight(replace_once(preflight,
   R"("writableHint":false)",R"("writableHint":true)"));
 invalid_preflight(replace_once(preflight,
   R"("entries":[)",R"("entries":[])"));
 invalid_preflight(preflight+" garbage");
 invalid_preflight(preflight.substr(0,preflight.size()-1));
 CHECK(!cmp5_native::parse_preflight(preflight.data(),preflight.size(),nullptr));
 CHECK(!cmp5_native::parse_preflight(nullptr,0,&permissions));
 puts("Bounded native preflight decoding: PASS");
 puts("Bounded native overview decoding: PASS");
 return 0;
}
