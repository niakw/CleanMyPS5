// CleanMyPS5: least-privilege actor/operation boundary.
// GPL-3.0-or-later. Read access and euid=0 never authorize deletion.
#pragma once
namespace cmp5_privilege {
enum class Actor { native_title, diagnostic_helper, compression_engine };
enum class Operation {
 inspect_metadata, request_compression, execute_compression,
 backup_private_media, delete_gallery_media, clean_system_cache, uninstall_title,
 change_autoload, restore_gallery, elevate_credentials
};
constexpr bool allows(Actor actor,Operation op) noexcept {
 switch(op){
 case Operation::inspect_metadata:
  return actor==Actor::native_title||actor==Actor::diagnostic_helper;
 case Operation::request_compression: return actor==Actor::native_title;
 case Operation::backup_private_media: return actor==Actor::native_title;
 case Operation::execute_compression: return actor==Actor::compression_engine;
 default: return false;
 }
}
constexpr bool can_delete(Actor actor) noexcept {
 return allows(actor,Operation::delete_gallery_media)||
        allows(actor,Operation::clean_system_cache)||
        allows(actor,Operation::uninstall_title);
}
static_assert(allows(Actor::native_title,Operation::backup_private_media));
static_assert(!allows(Actor::diagnostic_helper,Operation::backup_private_media));
static_assert(!allows(Actor::compression_engine,Operation::backup_private_media));
static_assert(!can_delete(Actor::native_title));
static_assert(!can_delete(Actor::diagnostic_helper));
static_assert(!can_delete(Actor::compression_engine));
} // namespace cmp5_privilege
