// Copyright (c) 2018, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_TYPE_TESTING_STUBS_H_
#define RUNTIME_VM_TYPE_TESTING_STUBS_H_

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/il.h"

namespace dart {

class TypeTestingStubNamer {
 public:
  TypeTestingStubNamer();

  // Simple helper for stringinfying a [type] and prefix it with the type
  // testing
  //
  // (only during dart_boostrap).
  const char* StubNameForType(const AbstractType& type) const;

 private:
  const char* StringifyType(const AbstractType& type) const;
  static const char* AssemblerSafeName(char* cname);

  Library& lib_;
  Class& klass_;
  AbstractType& type_;
  TypeArguments& type_arguments_;
  String& string_;
};

class TypeTestingStubGenerator {
 public:
  // During bootstrapping it will return `null` for a whitelisted set of types,
  // otherwise it will return a default stub which tail-calls
  // subtypingtest/runtime code.
  static RawInstructions* DefaultCodeForType(const AbstractType& type);

  TypeTestingStubGenerator();

  // Creates new stub for [type] (and registers the tuple in object store
  // array) or returns default stub.
  RawInstructions* OptimizedCodeForType(const AbstractType& type);

 private:
#if !defined(TARGET_ARCH_DBC) && !defined(TARGET_ARCH_IA32)
#if !defined(DART_PRECOMPILED_RUNTIME)
  RawInstructions* BuildCodeForType(const Type& type);
  static void BuildOptimizedTypeTestStub(Assembler* assembler,
                                         HierarchyInfo* hi,
                                         const Type& type,
                                         const Class& type_class);

  static void BuildOptimizedTypeTestStubFastCases(Assembler* assembler,
                                                  HierarchyInfo* hi,
                                                  const Type& type,
                                                  const Class& type_class,
                                                  Register instance_reg,
                                                  Register class_id_reg);

  static void BuildOptimizedSubtypeRangeCheck(Assembler* assembler,
                                              const CidRangeVector& ranges,
                                              Register class_id_reg,
                                              Register instance_reg,
                                              bool smi_is_ok);

  static void BuildOptimizedSubclassRangeCheckWithTypeArguments(
      Assembler* assembler,
      HierarchyInfo* hi,
      const Class& type_class,
      const TypeArguments& type_parameters,
      const TypeArguments& type_arguments);

  static void BuildOptimizedSubclassRangeCheckWithTypeArguments(
      Assembler* assembler,
      HierarchyInfo* hi,
      const Class& type_class,
      const TypeArguments& type_parameters,
      const TypeArguments& type_arguments,
      const Register class_id_reg,
      const Register instance_reg,
      const Register instance_type_args_reg);

  static void BuildOptimizedSubclassRangeCheck(Assembler* assembler,
                                               const CidRangeVector& ranges,
                                               Register class_id_reg,
                                               Register instance_reg,
                                               Label* check_failed);

  static void BuildOptimizedTypeArgumentValueCheck(
      Assembler* assembler,
      HierarchyInfo* hi,
      const AbstractType& type_arg,
      intptr_t type_param_value_offset_i,
      Label* check_failed);

  static void BuildOptimizedTypeArgumentValueCheck(
      Assembler* assembler,
      HierarchyInfo* hi,
      const AbstractType& type_arg,
      intptr_t type_param_value_offset_i,
      const Register class_id_reg,
      const Register instance_type_args_reg,
      const Register instantiator_type_args_reg,
      const Register function_type_args_reg,
      const Register type_arg_reg,
      Label* check_failed);

#endif  // !defined(DART_PRECOMPILED_RUNTIME)
#endif  // !defined(TARGET_ARCH_DBC) && !defined(TARGET_ARCH_IA32)

  TypeTestingStubNamer namer_;
  ObjectStore* object_store_;
  GrowableObjectArray& array_;
  Instructions& instr_;
};

// It is assumed that the caller ensures, while this object lives there is no
// other access to [Isolate::Current()->object_store()->type_testing_stubs()].
class TypeTestingStubFinder {
 public:
  TypeTestingStubFinder();

  // When serializing an AOT snapshot via our clustered snapshot writer, we
  // write out references to the [Instructions] object for all the
  // [AbstractType] objects we encounter.
  //
  // This method is used for this mapping of stub entrypoint addresses to the
  // corresponding [Instructions] object.
  RawInstructions* LookupByAddresss(uword entry_point) const;

  // When generating an AOT snapshot as an assembly file (i.e. ".S" file) we
  // need to generate labels for the type testing stubs.
  //
  // This method maps stub entrypoint addresses to meaningful names.
  const char* StubNameFromAddresss(uword entry_point) const;

 private:
  // Sorts the tuples in [array_] according to entrypoint.
  void SortTableForFastLookup();

  // Returns the tuple index where [entry_point] was found.
  intptr_t LookupInSortedArray(uword entry_point) const;

  TypeTestingStubNamer namer_;
  GrowableObjectArray& array_;
  AbstractType& type_;
  Code& code_;
  Instructions& instr_;
};

template <typename T>
class ReusableHandleStack {
 public:
  explicit ReusableHandleStack(Zone* zone) : zone_(zone), handles_count_(0) {}

  T* Obtain() {
    T* handle;
    if (handles_count_ < handles_.length()) {
      handle = handles_[handles_count_];
    } else {
      handle = &T::ZoneHandle(zone_);
      handles_.Add(handle);
    }
    handles_count_++;
    return handle;
  }

  // The released [handle] can be used even after releasing until the next
  // call to [Obtain].
  void Release(T* handle) {
    handles_count_--;
    ASSERT(handles_count_ >= 0);
    ASSERT(handles_[handles_count_] == handle);
  }

  Zone* zone_;

  intptr_t handles_count_;
  MallocGrowableArray<T*> handles_;
};

// Attempts to find a [Class] from un-instantiated [TypeArgument] vector to
// which it's type parameters are referring to.
//
// If the given type argument vector contains references to type parameters,
// this finder will either return a valid class if all of the type parameters
// come from the same class and returns `null` otherwise.
//
// It is safe to use this class inside loops since the implementation uses a
// [ReusableHandleStack] (which in pratice will only use a handful of handles).
class TypeArgumentClassFinder {
 public:
  explicit TypeArgumentClassFinder(Zone* zone)
      : klass_(Class::Handle(zone)),
        type_(AbstractType::Handle(zone)),
        type_arguments_handles_(zone) {}

  const Class& FindClass(const TypeArguments& ta) {
    klass_ = Class::null();

    const intptr_t len = ta.Length();
    for (intptr_t i = 0; i < len; ++i) {
      type_ = ta.TypeAt(i);
      if (!FindClassFromType(type_)) {
        klass_ = Class::null();
        break;
      }
    }
    return klass_;
  }

 private:
  bool FindClassFromType(const AbstractType& type) {
    if (type.IsTypeParameter()) {
      const TypeParameter& parameter = TypeParameter::Cast(type);
      if (!parameter.IsClassTypeParameter()) {
        return false;
      }
      if (klass_.IsNull()) {
        klass_ = parameter.parameterized_class();
      } else {
        // Dart has no support for nested classes.
        ASSERT(klass_.raw() == parameter.parameterized_class());
      }
      return true;
    } else if (type.IsFunctionType()) {
      // No support for function types yet.
      return false;
    } else if (type.IsTypeRef()) {
      // No support for recursive types.
      return false;
    } else if (type.IsType()) {
      TypeArguments* type_arguments = type_arguments_handles_.Obtain();
      *type_arguments = Type::Cast(type).arguments();
      const intptr_t len = type_arguments->Length();
      for (intptr_t i = 0; i < len; ++i) {
        type_ = type_arguments->TypeAt(i);
        if (!FindClassFromType(type_)) {
          type_arguments_handles_.Release(type_arguments);
          return false;
        }
      }
      type_arguments_handles_.Release(type_arguments);
      return true;
    } else if (type.IsBoundedType()) {
      // No support for bounded types.
      return false;
    }
    UNREACHABLE();
    return false;
  }

  Class& klass_;
  AbstractType& type_;

  ReusableHandleStack<TypeArguments> type_arguments_handles_;
};

// Used for instantiating a [TypeArguments] which contains references to type
// parameters based on an instantiator [TypeArguments] vector.
//
// It is safe to use this class inside loops since the implementation uses a
// [ReusableHandleStack] (which in pratice will only use a handful of handles).
class TypeArgumentInstantiator {
 public:
  explicit TypeArgumentInstantiator(Zone* zone)
      : klass_(Class::Handle(zone)),
        type_(AbstractType::Handle(zone)),
        instantiator_type_arguments_(TypeArguments::Handle(zone)),
        type_arguments_handles_(zone),
        type_handles_(zone) {}

  RawTypeArguments* Instantiate(
      const Class& klass,
      const TypeArguments& type_arguments,
      const TypeArguments& instantiator_type_arguments) {
    instantiator_type_arguments_ = instantiator_type_arguments.raw();
    return InstantiateTypeArguments(klass, type_arguments).raw();
  }

 private:
  const TypeArguments& InstantiateTypeArguments(
      const Class& klass,
      const TypeArguments& type_arguments);

  RawAbstractType* InstantiateType(const AbstractType& type);

  Class& klass_;
  AbstractType& type_;
  TypeArguments& instantiator_type_arguments_;

  ReusableHandleStack<TypeArguments> type_arguments_handles_;
  ReusableHandleStack<Type> type_handles_;
};

// Collects data on how [Type] objects are used in generated code.
class TypeUsageInfo : public StackResource {
 public:
  explicit TypeUsageInfo(Thread* thread);
  ~TypeUsageInfo();

  void UseTypeInAssertAssignable(const AbstractType& type);
  void UseTypeArgumentsInInstanceCreation(const Class& klass,
                                          const TypeArguments& ta);

  // Finalize the collected type usage information.
  void BuildTypeUsageInformation();

  // Query if [type] is very likely used in a type test (can give
  // false-positives and false-negatives, but tries to make a very good guess)
  bool IsUsedInTypeTest(const AbstractType& type);

 private:
  template <typename T>
  class ObjectSetTrait {
   public:
    // Typedefs needed for the DirectChainedHashMap template.
    typedef const T* Key;
    typedef const T* Value;
    typedef const T* Pair;

    static Key KeyOf(Pair kv) { return kv; }
    static Value ValueOf(Pair kv) { return kv; }
    static inline intptr_t Hashcode(Key key) { return key->Hash(); }
  };

  class TypeSetTrait : public ObjectSetTrait<const AbstractType> {
   public:
    static inline bool IsKeyEqual(const AbstractType* pair,
                                  const AbstractType* key) {
      return pair->Equals(*key);
    }
  };

  class TypeArgumentsSetTrait : public ObjectSetTrait<const TypeArguments> {
   public:
    static inline bool IsKeyEqual(const TypeArguments* pair,
                                  const TypeArguments* key) {
      return pair->raw() == key->raw();
    }
  };

  class TypeParameterSetTrait : public ObjectSetTrait<const TypeParameter> {
   public:
    static inline bool IsKeyEqual(const TypeParameter* pair,
                                  const TypeParameter* key) {
      return pair->raw() == key->raw();
    }
  };

  typedef DirectChainedHashMap<TypeSetTrait> TypeSet;
  typedef DirectChainedHashMap<TypeArgumentsSetTrait> TypeArgumentsSet;
  typedef DirectChainedHashMap<TypeParameterSetTrait> TypeParameterSet;

  // Runs an (early terminated) fix-point algorithm which propagates type
  // arguments.  For example:
  //
  //   class Base<X> {}
  //
  //   class Foo<A, B> extends Base<B> {
  //     foo() => new Map<List<B>, A>();
  //   }
  //
  //   main() {
  //     new Foo<String, int>();
  //     new Map<double, bool>();
  //   }
  //
  // will end up adding new type argument vectors to the per-class instantiator
  // type argument vector set:
  //
  //   Foo:
  //     <int, String, int>
  //   Map:
  //     <List<int>, String>
  //     <double, bool>
  //
  void PropagateTypeArguments(ClassTable* class_table, intptr_t cid_count);

  // Collects all type parameters we are doing assert assignable checks against.
  void CollectTypeParametersUsedInAssertAssignable(TypeParameterSet* set);

  // All types which flow into any of the type parameters in [set] will be added
  // to the set of types we test against.
  void UpdateAssertAssignableTypes(ClassTable* class_table,
                                   intptr_t cid_count,
                                   TypeParameterSet* set);

  void AddToSetIfParameter(TypeParameterSet* set,
                           const AbstractType* type,
                           TypeParameter* param);
  void AddTypeToSet(TypeSet* set, const AbstractType* type);

  Zone* zone_;
  TypeArgumentClassFinder finder_;
  TypeSet assert_assignable_types_;
  TypeArgumentsSet* instance_creation_arguments_;

  Class& klass_;
};

void RegisterTypeArgumentsUse(const Function& function,
                              TypeUsageInfo* type_usage_info,
                              const Class& klass,
                              Definition* type_arguments);

}  // namespace dart

#endif  // RUNTIME_VM_TYPE_TESTING_STUBS_H_
