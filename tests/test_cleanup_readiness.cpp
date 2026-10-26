#include "../native/cleanup_readiness.hpp"
#include "../native/privilege_contract.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace cmp5_cleanup;
int main(){
 for(unsigned i=0;i<static_cast<unsigned>(Category::count)+2u;i++){
  const auto cat=static_cast<Category>(i);
  // All conceivable evidence, including synthetic user consent, cannot
  // transform a read-only report into a PS5 deletion operation.
  for(std::uint32_t proofs=0;proofs<=all_proofs;proofs++){
   const auto full=assess(Evidence{cat,true,false,1u,proofs});
   assert(!full.can_delete);
   const auto partial=assess(Evidence{cat,true,true,1u,proofs});
   assert(!partial.can_delete&&!partial.can_review);
   assert(partial.first_blocker==Blocker::scope_partial);
  }
 }
 assert(assess(Evidence{}).first_blocker==Blocker::not_scanned);
 assert(assess(Evidence{Category::gallery,true,false,0u,0u})
   .first_blocker==Blocker::no_observation);
 assert(assess(Evidence{Category::gallery,true,false,2u,0u})
   .first_blocker==Blocker::references_unverified);
 // Eight installed apps/plugins (or any other count) cannot become
 // eight proposed caches. Partial/inaccessible scans remain partial.
 const auto app_observation=unclassified_app_inventory(true,false,8u);
 assert(app_observation.scanned&&app_observation.observed==0u&&
        app_observation.proofs==0u);
 const auto app_readiness=assess(app_observation);
 assert(!app_readiness.can_review&&!app_readiness.can_delete&&
        app_readiness.first_blocker==Blocker::no_observation);
 const auto app_partial=assess(unclassified_app_inventory(true,true,100u));
 assert(!app_partial.can_review&&!app_partial.can_delete&&
        app_partial.first_blocker==Blocker::scope_partial);
 assert(assess(unclassified_app_inventory(false,false,100u)).first_blocker==
        Blocker::not_scanned);
 assert(assess(Evidence{Category::homebrew_cache,true,false,2u,0u})
   .first_blocker==Blocker::provider_unverified);
 assert(assess(Evidence{Category::autoload,true,false,2u,0u})
   .first_blocker==Blocker::process_active_unknown);
 const auto gallery=assess(Evidence{Category::gallery,true,false,2u,
   owner|provider});
 assert(gallery.can_review&&!gallery.can_delete);
 assert((gallery.missing_proofs&references)!=0u);
 assert(gallery.first_blocker==Blocker::references_unverified);
 assert(required_proofs(Category::exact_duplicates)&references);
 assert(required_proofs(Category::autoload)&processes_idle);
 assert((required_proofs(Category::homebrew_cache)&references)==0u);
 for(auto cat:{Category::system_cache,Category::game_images,
               Category::unknown_residues,static_cast<Category>(255u)}){
  const auto state=assess(Evidence{cat,true,false,50u,all_proofs});
  assert(state.can_review&&!state.can_delete);
  assert(state.first_blocker==Blocker::protected_content);
 }
 for(auto cat:{Category::gallery,Category::exact_duplicates,
               Category::homebrew_cache,Category::autoload}){
  const auto state=assess(Evidence{cat,true,false,5u,all_proofs});
  assert(state.can_review&&!state.can_delete);
  assert(state.missing_proofs==0u);
  assert(state.first_blocker==Blocker::console_mutation_unavailable);
 }
 assert(cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
   cmp5_privilege::Operation::backup_private_media));
 assert(cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
   cmp5_privilege::Operation::verify_private_media));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::diagnostic_helper,
   cmp5_privilege::Operation::verify_private_media));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::compression_engine,
   cmp5_privilege::Operation::verify_private_media));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::diagnostic_helper,
   cmp5_privilege::Operation::backup_private_media));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::compression_engine,
   cmp5_privilege::Operation::backup_private_media));
 assert(cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
   cmp5_privilege::Operation::discard_private_rehearsal));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::diagnostic_helper,
   cmp5_privilege::Operation::discard_private_rehearsal));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::compression_engine,
   cmp5_privilege::Operation::discard_private_rehearsal));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
   cmp5_privilege::Operation::delete_gallery_media));
 assert(!cmp5_privilege::allows(cmp5_privilege::Actor::native_title,
   cmp5_privilege::Operation::clean_system_cache));
 assert(!cmp5_privilege::can_delete(cmp5_privilege::Actor::native_title));
 assert(!cmp5_privilege::can_delete(cmp5_privilege::Actor::diagnostic_helper));
 assert(!cmp5_privilege::can_delete(cmp5_privilege::Actor::compression_engine));
 std::puts("Category-wide cleanup readiness: no implicit delete authorization: PASS");
}
