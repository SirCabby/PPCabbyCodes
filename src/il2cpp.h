#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The IL2CPP runtime's C API (exported by GameAssembly.so, GameAssembly.dll on
// Windows), and helpers over it. Everything the mod knows about the game's classes it looks up here by
// name - assemblies, classes, methods, field offsets - so nothing depends on
// the addresses or layouts of one build.
namespace ppcc::il2cpp {

struct Domain;
struct Assembly;
struct Image;
struct Class;
struct Type;
struct Field;
struct Object {
  Class* klass;
  void* monitor;
};
// IL2CPP's MethodInfo begins with the compiled code's address.
struct Method {
  void* pointer;
};
struct String : Object {
  int32_t length;
  char16_t chars[1];
};
struct Array : Object {
  void* bounds;
  uintptr_t max_length;
  // elements follow at +0x20
};

#define PPCC_IL2CPP_API(X)                                                                  \
  X(Domain*, il2cpp_domain_get, ())                                                         \
  X(const Assembly**, il2cpp_domain_get_assemblies, (const Domain*, size_t*))               \
  X(const Image*, il2cpp_assembly_get_image, (const Assembly*))                             \
  X(const char*, il2cpp_image_get_name, (const Image*))                                     \
  X(Class*, il2cpp_class_from_name, (const Image*, const char*, const char*))               \
  X(const Method*, il2cpp_class_get_method_from_name, (Class*, const char*, int))           \
  X(const Method*, il2cpp_class_get_methods, (Class*, void**))                              \
  X(const char*, il2cpp_method_get_name, (const Method*))                                   \
  X(uint32_t, il2cpp_method_get_param_count, (const Method*))                               \
  X(Field*, il2cpp_class_get_field_from_name, (Class*, const char*))                        \
  X(size_t, il2cpp_field_get_offset, (Field*))                                              \
  X(void, il2cpp_field_static_get_value, (Field*, void*))                                   \
  X(Class*, il2cpp_class_get_parent, (Class*))                                              \
  X(const char*, il2cpp_class_get_name, (Class*))                                           \
  X(const char*, il2cpp_class_get_namespace, (Class*))                                      \
  X(bool, il2cpp_class_is_assignable_from, (Class*, Class*))                                \
  X(void, il2cpp_runtime_class_init, (Class*))                                              \
  X(Class*, il2cpp_object_get_class, (Object*))                                             \
  X(String*, il2cpp_string_new, (const char*))                                              \
  X(Object*, il2cpp_runtime_invoke, (const Method*, void*, void**, Object**))               \
  X(void*, il2cpp_resolve_icall, (const char*))                                             \
  X(Object*, il2cpp_value_box, (Class*, void*))                                             \
  X(void*, il2cpp_object_unbox, (Object*))                                                  \
  X(const Image*, il2cpp_get_corlib, ())                                                     \
  X(const Method*, il2cpp_object_get_virtual_method, (Object*, const Method*))

#define PPCC_DECLARE(ret, name, args) extern ret(*name) args;
PPCC_IL2CPP_API(PPCC_DECLARE)
#undef PPCC_DECLARE

// Resolves the API from GameAssembly's handle (dlopen's, or its HMODULE).
// False (logged) when any function the mod needs is missing.
bool bind(void* game_assembly);
bool bound();
void* library();  // the handle bind() was given

// The image (assembly) with this name, e.g. "Assembly-CSharp.dll".
const Image* image(const char* name);

// A class by namespace and name; nested classes as "Outer/Inner". Logs a miss.
Class* find_class(const char* image_name, const char* ns, const char* name);

// A method of `klass` or its bases (argc -1: any).
const Method* find_method(Class* klass, const char* name, int argc);

// A field's offset in an object of `klass` (or a base), or -1. Logs a miss.
int field_offset(Class* klass, const char* name);

// The UTF-8 text of a managed string (empty for null).
std::string to_utf8(const String* s);

bool is_instance(const Object* o, Class* klass);
const char* class_name(const Object* o);

// Typed reads and writes of an object's fields by offset. Offsets come from
// field_offset; a negative offset reads as zero/null and writes nothing.
template <typename T>
inline T get(const void* obj, int off) {
  if (!obj || off < 0) return T{};
  T v;
  __builtin_memcpy(&v, static_cast<const char*>(obj) + off, sizeof(T));
  return v;
}
template <typename T>
inline void set(void* obj, int off, const T& v) {
  if (!obj || off < 0) return;
  __builtin_memcpy(static_cast<char*>(obj) + off, &v, sizeof(T));
}

// Element i of a managed array (object[] etc.), or null when out of range.
inline Object* array_at(const Array* a, size_t i) {
  if (!a || i >= a->max_length) return nullptr;
  return reinterpret_cast<Object* const*>(reinterpret_cast<const char*>(a) + 0x20)[i];
}

}  // namespace ppcc::il2cpp
