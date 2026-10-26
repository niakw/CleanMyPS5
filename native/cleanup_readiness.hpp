// CleanMyPS5 — category-wide, non-destructive cleanup readiness contract.
// A scanner observation and even an independently verified private restore
// NEVER grant the title a PS5 deletion capability. GPL-3.0-or-later.
#pragma once
#include <cstdint>

namespace cmp5_cleanup {
enum class Category : unsigned {
 gallery, exact_duplicates, homebrew_cache, autoload,
 game_images, unknown_residues, system_cache, count
};
enum class Blocker : unsigned {
 not_scanned, scope_partial, no_observation, protected_content,
 owner_unverified, provider_unverified, references_unverified,
 backup_unverified, restore_unverified, permissions_unverified,
 process_active_unknown, source_changed, approval_missing,
 console_mutation_unavailable
};
enum Proof : std::uint32_t {
 owner=1u<<0, provider=1u<<1, references=1u<<2,
 verified_backup=1u<<3, verified_restore=1u<<4,
 permissions=1u<<5, processes_idle=1u<<6,
 source_identity=1u<<7, explicit_approval=1u<<8
};
constexpr std::uint32_t all_proofs=
 owner|provider|references|verified_backup|verified_restore|
 permissions|processes_idle|source_identity|explicit_approval;
struct Evidence {
 Category category=Category::gallery;
 bool scanned=false;
 bool partial=true;
 std::uint32_t observed=0; // count of nominal observations, never deletions
 std::uint32_t proofs=0;    // only from independent proofs, NOT from scan counts
};
struct Assessment {
 Blocker first_blocker=Blocker::not_scanned;
 std::uint32_t missing_proofs=0;
 bool can_review=false;
 bool can_delete=false; // PS5 deletion is deliberately not implemented
};
/* A homebrew inventory counts applications, plugins and app directories.
 * It does NOT establish that any entry is a disposable cache. Keep the
 * count for diagnosis, but do not promote it to cleanup observations until
 * a separate scanner identifies provider-owned temporary artefacts. */
constexpr Evidence unclassified_app_inventory(bool scanned,bool partial,
                                             std::uint32_t app_entries) noexcept {
 (void)app_entries;
 return Evidence{Category::homebrew_cache,scanned,partial,0u,0u};
}
constexpr bool protected_category(Category category) noexcept {
 return category==Category::game_images||
        category==Category::system_cache||
        category==Category::unknown_residues||
        static_cast<unsigned>(category)>=static_cast<unsigned>(Category::count);
}
constexpr std::uint32_t required_proofs(Category category) noexcept {
 if(protected_category(category))return 0u;
 const std::uint32_t common=owner|provider|verified_backup|verified_restore|
   permissions|source_identity|explicit_approval;
 if(category==Category::gallery||
    category==Category::exact_duplicates)return common|references;
 if(category==Category::autoload)return common|processes_idle;
 return common; // app cache: independent, verified owner/provider only
}
constexpr Blocker next_blocker(Category category,std::uint32_t missing) noexcept {
 // Prioritize the proof that is characteristic of each category.
 if((category==Category::gallery||
     category==Category::exact_duplicates)&&(missing&references))
  return Blocker::references_unverified;
 if(category==Category::homebrew_cache&&(missing&provider))
  return Blocker::provider_unverified;
 if(category==Category::autoload&&(missing&processes_idle))
  return Blocker::process_active_unknown;
 return (missing&owner)?Blocker::owner_unverified:
        (missing&provider)?Blocker::provider_unverified:
        (missing&references)?Blocker::references_unverified:
        (missing&verified_backup)?Blocker::backup_unverified:
        (missing&verified_restore)?Blocker::restore_unverified:
        (missing&permissions)?Blocker::permissions_unverified:
        (missing&processes_idle)?Blocker::process_active_unknown:
        (missing&source_identity)?Blocker::source_changed:
        (missing&explicit_approval)?Blocker::approval_missing:
        Blocker::console_mutation_unavailable;
}
constexpr Assessment assess(const Evidence&input) noexcept {
 Assessment result{};
 if(!input.scanned)return result;
 if(input.partial){
  result.first_blocker=Blocker::scope_partial;
  return result;
 }
 if(!input.observed){
  result.first_blocker=Blocker::no_observation;
  return result;
 }
 result.can_review=true;
 if(protected_category(input.category)){
  result.first_blocker=Blocker::protected_content;
  return result;
 }
 result.missing_proofs=required_proofs(input.category)&~input.proofs;
 result.first_blocker=next_blocker(input.category,result.missing_proofs);
 // There is NO executable deletion/cleanup path in the installed title.
 // Runtime flags, owner=euid(0), synthetic fixture results, or an
 // authenticated helper must not turn an observation into mutation rights.
 return result;
}
constexpr const char*reason_fr(Blocker b) noexcept {
 switch(b){
 case Blocker::not_scanned:return "NON VERIFIE";
 case Blocker::scope_partial:return "INVENTAIRE PARTIEL";
 case Blocker::no_observation:return "AUCUN CANDIDAT OBSERVE";
 case Blocker::protected_content:return "CONTENU PROTEGE  REVUE SEULE";
 case Blocker::owner_unverified:return "PROPRIETAIRE NON ATTESTE";
 case Blocker::provider_unverified:return "FOURNISSEUR NON ATTESTE";
 case Blocker::references_unverified:return "DEPENDANCES NON PROUVEES";
 case Blocker::backup_unverified:return "SAUVEGARDE NON PROUVEE";
 case Blocker::restore_unverified:return "RESTAURATION NON PROUVEE";
 case Blocker::permissions_unverified:return "DROITS NON PROUVES";
 case Blocker::process_active_unknown:return "PROCESSUS NON VERIFIES";
 case Blocker::source_changed:return "IDENTITE SOURCE NON PROUVEE";
 case Blocker::approval_missing:return "ACCORD EXPLICITE MANQUANT";
 default:return "NETTOYAGE PS5 DESACTIVE";
 }
}
static_assert(!assess(Evidence{Category::gallery,true,false,1u,all_proofs}).can_delete);
static_assert(!assess(Evidence{Category::homebrew_cache,true,false,1u,all_proofs}).can_delete);
static_assert(!assess(Evidence{Category::autoload,true,false,1u,all_proofs}).can_delete);
} // namespace cmp5_cleanup
