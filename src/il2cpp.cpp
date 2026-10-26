#include "il2cpp.h"

#include <cstring>

#include "log.h"
#include "platform.h"

namespace ppcc::il2cpp {

#define PPCC_DEFINE(ret, name, args) ret(*name) args = nullptr;
PPCC_IL2CPP_API(PPCC_DEFINE)
#undef PPCC_DEFINE

namespace {
bool g_bound = false;
void* g_library = nullptr;
}

bool bind(void* game_assembly) {
  g_library = game_assembly;
  int missing = 0;
#define PPCC_RESOLVE(ret, name, args)                                           \
  name = reinterpret_cast<ret(*) args>(platform::symbol(game_assembly, #name)); \
  if (!name) {                                                                  \
    logf("il2cpp: %s is not exported", #name);                                  \
    ++missing;                                                                  \
  }
  PPCC_IL2CPP_API(PPCC_RESOLVE)
#undef PPCC_RESOLVE
  g_bound = missing == 0;
  return g_bound;
}

bool bound() { return g_bound; }

void* library() { return g_library; }

const Image* image(const char* name) {
  size_t n = 0;
  const Assembly** list = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
  for (size_t i = 0; i < n; ++i) {
    const Image* img = il2cpp_assembly_get_image(list[i]);
    if (img && std::strcmp(il2cpp_image_get_name(img), name) == 0) return img;
  }
  return nullptr;
}

Class* find_class(const char* image_name, const char* ns, const char* name) {
  const Image* img = image(image_name);
  if (!img) {
    logf("il2cpp: no image %s", image_name);
    return nullptr;
  }
  // Nested classes: il2cpp_class_from_name finds them by "Outer/Inner".
  Class* k = il2cpp_class_from_name(img, ns, name);
  if (!k) logf("il2cpp: no class %s%s%s in %s", ns, *ns ? "." : "", name, image_name);
  return k;
}

const Method* find_method(Class* klass, const char* name, int argc) {
  for (Class* k = klass; k; k = il2cpp_class_get_parent(k)) {
    if (const Method* m = il2cpp_class_get_method_from_name(k, name, argc)) return m;
  }
  if (klass) logf("il2cpp: no method %s.%s/%d", il2cpp_class_get_name(klass), name, argc);
  return nullptr;
}

int field_offset(Class* klass, const char* name) {
  for (Class* k = klass; k; k = il2cpp_class_get_parent(k)) {
    if (Field* f = il2cpp_class_get_field_from_name(k, name)) return static_cast<int>(il2cpp_field_get_offset(f));
  }
  if (klass) logf("il2cpp: no field %s.%s", il2cpp_class_get_name(klass), name);
  return -1;
}

std::string to_utf8(const String* s) {
  std::string out;
  if (!s || s->length <= 0) return out;
  out.reserve(static_cast<size_t>(s->length));
  for (int32_t i = 0; i < s->length; ++i) {
    uint32_t c = s->chars[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < s->length) {
      const uint32_t lo = s->chars[i + 1];
      if (lo >= 0xDC00 && lo < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        ++i;
      }
    }
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

bool is_instance(const Object* o, Class* klass) {
  if (!o || !klass) return false;
  return il2cpp_class_is_assignable_from(klass, o->klass);
}

const char* class_name(const Object* o) { return o ? il2cpp_class_get_name(o->klass) : "null"; }

}  // namespace ppcc::il2cpp
