// Copyright 2008 Google Inc. All Rights Reserved.
//
// Various Google-specific macros.
//
// This code is compiled directly on many platforms, including client
// platforms like Windows, Mac, and embedded systems.  Before making
// any changes here, make sure that you're not breaking any platforms.

#ifndef BASE_MACROS_H_
#define BASE_MACROS_H_

#include <cstddef>

// TODO: Simplify this and callers to just use the standard [[assume]]
// once we have compiler support everywhere.
#ifdef __has_cpp_attribute
  #if __has_cpp_attribute(assume) >= 202207L
    #define ASSUME(...) [[assume(__VA_ARGS__)]]
  #endif
#endif
#ifndef ASSUME
  #if defined(__clang__)
    #define ASSUME(...) __builtin_assume(__VA_ARGS__)
  #elif defined(__GNUC__) && __GNUC__ >= 13
    #define ASSUME(...) __attribute__((__assume__(__VA_ARGS__)))
  #endif
#endif

// TODO: [[pure]] may be available in like c++26.
#if defined(__clang__) || defined(__GNUC__)
#define PURE_FN __attribute__((pure))
#else
#define PURE_FN
#endif

// Assert that a loop does not have memory dependencies across
// iterations; no memory location written in one iteration may
// be accessed (read or written) in a different iteration. The
// typical application is for a loop where you know the regions
// of memory do not overlap and you want to allow for full
// vectorization. You can still have effects in the loop like
// accumulating to local variables.
//
// This must be applied exactly to a for, while, or do loop. To
// prevent accidental misuse, use it like this:
// NO_LOOP_CARRIED_DEPENDENCIES(for) (int x = 0; x < size; x++) ...
#if defined(__clang__)
#define NO_LOOP_CARRIED_DEPENDENCIES(loop_kw) _Pragma("clang loop vectorize(assume_safety)") loop_kw
#elif defined(__GNUC__)
#define NO_LOOP_CARRIED_DEPENDENCIES(loop_kw) _Pragma("GCC ivdep") loop_kw
#elif defined(_MSC_VER)
#define NO_LOOP_CARRIED_DEPENDENCIES(loop_kw) __pragma(loop(ivdep)) loop_kw
#else
#define NO_LOOP_CARRIED_DEPENDENCIES(loop_kw) loop_kw
#endif

// A macro to disallow the copy constructor and operator= functions
// This is typically used in the private: declarations for a class.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete;

// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

// The arraysize(arr) macro returns the # of elements in an array arr.
// The expression is a compile-time constant, and therefore can be
// used in defining new arrays, for example.  If you use arraysize on
// a pointer by mistake, you will get a compile-time error.
//
// One caveat is that arraysize() doesn't accept any array of an
// anonymous type or a type defined inside a function.  In these rare
// cases, you have to use the unsafe ARRAYSIZE() macro below.  This is
// due to a limitation in C++'s template system.  The limitation might
// eventually be removed, but it hasn't happened yet.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];

// That gcc wants both of these prototypes seems mysterious. VC, for
// its part, can't decide which to use (another mystery). Matching of
// template overloads: the final frontier.
#ifndef COMPILER_MSVC
template <typename T, size_t N>
char (&ArraySizeHelper(const T (&array)[N]))[N];
#endif

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

// ARRAYSIZE performs essentially the same calculation as arraysize,
// but can be used on anonymous types or types defined inside
// functions.  It's less safe than arraysize as it accepts some
// (although not all) pointers.  Therefore, you should use arraysize
// whenever possible.
//
// The expression ARRAYSIZE(a) is a compile-time constant of type
// size_t.
//
// ARRAYSIZE catches a few type errors.  If you see a compiler error
//
//   "warning: division by zero in ..."
//
// when using ARRAYSIZE, you are (wrongfully) giving it a pointer.
// You should only use ARRAYSIZE on statically allocated arrays.
//
// The following comments are on the implementation details, and can
// be ignored by the users.
//
// ARRAYSIZE(arr) works by inspecting sizeof(arr) (the # of bytes in
// the array) and sizeof(*(arr)) (the # of bytes in one array
// element).  If the former is divisible by the latter, perhaps arr is
// indeed an array, in which case the division result is the # of
// elements in the array.  Otherwise, arr cannot possibly be an array,
// and we generate a compiler error to prevent the code from
// compiling.
//
// Since the size of bool is implementation-defined, we need to cast
// !(sizeof(a) & sizeof(*(a))) to size_t in order to ensure the final
// result has type size_t.
//
// This macro is not perfect as it wrongfully accepts certain
// pointers, namely where the pointer size is divisible by the pointee
// size.  Since all our code has to go through a 32-bit compiler,
// where a pointer is 4 bytes, this means all pointers to a type whose
// size is 3 or greater than 4 will be (righteously) rejected.
//
// Kudos to Jorg Brown for this simple and elegant implementation.
//
// - wan 2005-11-16
//
// Starting with Visual C++ 2005, WinNT.h includes ARRAYSIZE.
#if !defined(COMPILER_MSVC) || (defined(_MSC_VER) && _MSC_VER < 1400)
// This can also be present when using MINGW, but it's not the
// case that every MINGW program includes windows.h. Check for
// the actual define in winnt.h. -tom7  22 May 2016
#if !defined(__MINGW32__) || !defined(_WINNT_)
#define ARRAYSIZE(a) \
  ((sizeof(a) / sizeof(*(a))) / \
   static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
#endif
#endif

// A macro to turn a symbol into a string
#define AS_STRING(x)   AS_STRING_INTERNAL(x)
#define AS_STRING_INTERNAL(x)   #x


// One of the type traits, is_pod, makes it possible to query whether
// a type is a POD type. It is impossible for type_traits.h to get
// this right without compiler support, so it fails conservatively. It
// knows that fundamental types and pointers are PODs, but it can't
// tell whether user classes are PODs. The DECLARE_POD macro is used
// to inform the type traits library that a user class is a POD.
//
// Implementation note: the typedef at the end is just to make it legal
// to put a semicolon after DECLARE_POD(foo).
//
// The only reason this matters is that a few parts of the google3
// code base either require their template arguments to be PODs
// (e.g. compact_vector) or are able to use a more efficient code path
// when their template arguments are PODs (e.g. sparse_hash_map). You
// should use DECLARE_POD if you have written a class that you intend
// to use with one of those components, and if you know that your
// class satisfies all of the conditions to be a POD type.
//
// So what's a POD?  The C++ standard (clause 9 paragraph 4) gives a
// full definition, but a good rule of thumb is that a struct is a POD
// ("plain old data") if it doesn't use any of the features that make
// C++ different from C.  A POD struct can't have constructors,
// destructors, assignment operators, base classes, private or
// protected members, or virtual functions, and all of its member
// variables must themselves be PODs.

#define DECLARE_POD(TypeName)                       \
namespace base {                                    \
template<> struct is_pod<TypeName> : true_type { }; \
}                                                   \
typedef int Dummy_Type_For_DECLARE_POD              \

// We once needed a different technique to assert that a nested class
// is a POD. This is no longer necessary, and DECLARE_NESTED_POD is
// just a synonym for DECLARE_POD. We continue to provide
// DECLARE_NESTED_POD only so we don't have to change client
// code. Regardless of whether you use DECLARE_POD or
// DECLARE_NESTED_POD: use it after the outer class. Using it within a
// class definition will give a compiler error.
#define DECLARE_NESTED_POD(TypeName) DECLARE_POD(TypeName)

// Declare that TemplateName<T> is a POD whenever T is
#define PROPAGATE_POD_FROM_TEMPLATE_ARGUMENT(TemplateName)             \
namespace base {                                                       \
template <typename T> struct is_pod<TemplateName<T> > : is_pod<T> { }; \
}                                                                      \
typedef int Dummy_Type_For_PROPAGATE_POD_FROM_TEMPLATE_ARGUMENT

// Macro that does nothing if TypeName is a POD, and gives a compiler
// error if TypeName is a non-POD.  You should put a descriptive
// comment right next to the macro call so that people can tell what
// the compiler error is about.
//
// Implementation note: this works by taking the size of a type that's
// complete when TypeName is a POD and incomplete otherwise.

template <bool IsPod> struct ERROR_TYPE_MUST_BE_POD;
template <> struct ERROR_TYPE_MUST_BE_POD<true> { };
#define ENFORCE_POD(TypeName)                                             \
  enum { dummy_##TypeName                                                 \
           = sizeof(ERROR_TYPE_MUST_BE_POD<                               \
                      base::is_pod<TypeName>::value>) }

// Annotations that tell the compiler an argument is a printf-style
// format string.
#if (defined(__clang__) || defined(COMPILER_GCC3) || defined(COMPILER_ICC) || defined(OS_MACOSX))

// Tell the compiler to do printf format string checking if the
// compiler supports it; see the 'format' attribute in
// <http://gcc.gnu.org/onlinedocs/gcc-4.3.0/gcc/Function-Attributes.html>.
//
// e.g. printf would be marked PRINTF_ATTRIBUTE(1, 2) since the 1st arg
// is the format string and the 2nd onward are the arguments. sprintf
// would be marked PRINTF_ATTRIBUTE(2, 3). For functions that are
// (non-static) members of objects with an implicit 'this' parameter,
// use PRINTF_ATTRIBUTE_MEMBER.
#define PRINTF_ATTRIBUTE(string_index, first_to_check) \
    __attribute__((__format__ (__printf__, string_index, first_to_check)))
#define SCANF_ATTRIBUTE(string_index, first_to_check) \
    __attribute__((__format__ (__scanf__, string_index, first_to_check)))

// N.B.: As the GCC manual states, "[s]ince non-static C++ methods
// have an implicit 'this' argument, the arguments of such methods
// should be counted from two, not one."
#define PRINTF_ATTRIBUTE_MEMBER(string_index, first_to_check) \
  PRINTF_ATTRIBUTE(string_index + 1, first_to_check + 1)
#define SCANF_ATTRIBUTE_MEMBER(string_index, first_to_check) \
  SCANF_ATTRIBUTE(string_index + 1, first_to_check + 1)

#else
// Not known to be supported.

#define PRINTF_ATTRIBUTE(string_index, first_to_check)
#define SCANF_ATTRIBUTE(string_index, first_to_check)
#define PRINTF_ATTRIBUTE_MEMBER(string_index, first_to_check)
#define SCANF_ATTRIBUTE_MEMBER(string_index, first_to_check)

#endif


#endif  // BASE_MACROS_H_
