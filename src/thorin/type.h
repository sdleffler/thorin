#ifndef THORIN_TYPE_H
#define THORIN_TYPE_H

#include "thorin/def.h"
#include "thorin/util/array.h"
#include "thorin/util/hash.h"

namespace thorin {

class Generic;
class Lambda;
class Pi;
class PrimLit;
class Ptr;
class Type;
class World;

//------------------------------------------------------------------------------

class GenericMap {
public:
    GenericMap() {}

    const Type*& operator [] (const Generic* generic) const;
    bool is_empty() const;
    std::string to_string() const;

private:
    mutable std::vector<const Type*> types_;
};

//------------------------------------------------------------------------------

class Type : public MagicCast<Type> {
private:
    Type& operator = (const Type&); ///< Do not copy-assign a \p Type instance.
    Type(const Type&);             ///< Do not copy-construct a \p Type.

protected:
    Type(World& world, NodeKind kind, size_t num, bool is_generic)
        : world_(world)
        , kind_(kind)
        , elems_(num)
        , cur_pass_(0)
        , is_generic_(is_generic)
    {}

    void set(size_t i, const Type* n) { elems_[i] = n; }

public:
    NodeKind kind() const { return kind_; }
    bool is_corenode() const { return ::thorin::is_corenode(kind()); }
    ArrayRef<const Type*> elems() const { return elems_; }
    const Type* elem(size_t i) const { assert(i < elems().size()); return elems()[i]; }
    const Type* elem_via_lit(Def def) const;
    size_t size() const { return elems_.size(); }
    bool empty() const { return elems_.empty(); }
    void dump() const;
    World& world() const { return world_; }
    bool check_with(const Type* type) const;
    bool infer_with(GenericMap& map, const Type* type) const;
    const Type* specialize(const GenericMap&) const;
    bool is_generic() const { return is_generic_; }
    bool is_u1() const { return kind() == Node_PrimType_u1; }
    bool is_int() const { return thorin::is_int(kind()); }
    bool is_float() const { return thorin::is_float(kind()); }
    bool is_primtype() const { return thorin::is_primtype(kind()); }
    int order() const;
    virtual size_t hash() const;
    virtual bool equal(const Type* other) const;
    /**
     * Returns the vector length.
     * Raises an assertion if type of this is not a \p VectorType.
     */
    size_t length() const;

    size_t cur_pass() const { return cur_pass_; }
    bool visit(const size_t pass) const { 
        assert(cur_pass_ <= pass); 
        if (cur_pass_ != pass) { 
            cur_pass_ = pass; 
            return false; 
        } 
        return true; 
    }
    void visit_first(const size_t pass) const { assert(!is_visited(pass)); cur_pass_ = pass; }
    void unvisit(const size_t pass) const { assert(cur_pass_ == pass); --cur_pass_; }
    bool is_visited(const size_t pass) const { assert(cur_pass_ <= pass); return cur_pass_ == pass; }

private:
    World& world_;
    NodeKind kind_;
    std::vector<const Type*> elems_;
    mutable size_t cur_pass_;

protected:
    bool is_generic_;

    friend class DefNode;
    friend struct TypeHash;
    friend struct TypeEqual;
};

struct TypeHash { size_t operator () (const Type* t) const { return t->hash(); } };
struct TypeEqual { bool operator () (const Type* t1, const Type* t2) const { return t1->equal(t2); } };

//------------------------------------------------------------------------------

/// The type of the memory monad.
class Mem : public Type {
private:
    Mem(World& world)
        : Type(world, Node_Mem, 0, false)
    {}

    friend class World;
};

//------------------------------------------------------------------------------

/// The type of a stack frame.
class Frame : public Type {
private:
    Frame(World& world)
        : Type(world, Node_Frame, 0, false)
    {}

    friend class World;
};

//------------------------------------------------------------------------------

class VectorType : public Type {
protected:
    VectorType(World& world, NodeKind kind, size_t num_elems, size_t length, bool is_generic)
        : Type(world, kind, num_elems, is_generic)
        , length_(length)
    {}

    virtual size_t hash() const { return hash_combine(Type::hash(), length()); }
    virtual bool equal(const Type* other) const { 
        return Type::equal(other) ? this->length() == other->as<VectorType>()->length() : false;
    }

public:
    /// The number of vector elements - the vector length.
    size_t length() const { return length_; }
    bool is_vector() const { return length_ != 1; }
    /// Rebuilds the type with vector length 1.
    const VectorType* scalarize() const;

private:
    size_t length_;
};

//------------------------------------------------------------------------------

/// Primitive types -- also known as atomic or scalar types.
class PrimType : public VectorType {
private:
    PrimType(World& world, PrimTypeKind kind, size_t length)
        : VectorType(world, (NodeKind) kind, 0, length, false)
    {}

public:
    PrimTypeKind primtype_kind() const { return (PrimTypeKind) kind(); }

private:
    friend class World;
};

//------------------------------------------------------------------------------

class Ptr : public VectorType {
private:
    Ptr(World& world, const Type* referenced_type, size_t length)
        : VectorType(world, Node_Ptr, 1, length, referenced_type->is_generic())
    {
        set(0, referenced_type);
    }

public:
    const Type* referenced_type() const { return elem(0); }

    friend class World;
};

//------------------------------------------------------------------------------

class CompoundType : public Type {
protected:
    CompoundType(World& world, NodeKind kind, size_t num_elems);
    CompoundType(World& world, NodeKind kind, ArrayRef<const Type*> elems);
};

//------------------------------------------------------------------------------

/// A tuple type.
class Sigma : public CompoundType {
private:
    Sigma(World& world, size_t size, const std::string& name)
        : CompoundType(world, Node_Sigma, size)
        , name_(name)
    {
        assert(name != "");
    }
    Sigma(World& world, ArrayRef<const Type*> elems)
        : CompoundType(world, Node_Sigma, elems)
        , name_("")
    {}

    virtual size_t hash() const { return is_named() ? hash_value(this) : CompoundType::hash(); }
    virtual bool equal(const Type* other) const { return is_named() ? this == other : CompoundType::equal(other); }

public:
    bool is_named() const { return name_ == ""; }
    // TODO build setter for named sigmas which sets is_generic_

private:
    std::string name_;

    friend class World;
};

//------------------------------------------------------------------------------

/// A function type.
class Pi : public CompoundType {
private:
    Pi(World& world, ArrayRef<const Type*> elems)
        : CompoundType(world, Node_Pi, elems)
    {}

public:
    bool is_basicblock() const { return order() == 1; }
    bool is_returning() const;

    friend class World;
};

//------------------------------------------------------------------------------

class ArrayType : public Type {
private:
    ArrayType(World& world, const Type* elem_type)
        : Type(world, Node_ArrayType, 1, elem_type->is_generic())
    {
        set(0, elem_type);
    }

public:
    const Type* elem_type() const { return elem(0); }

    friend class World;
};

//------------------------------------------------------------------------------

class Generic : public Type {
private:
    Generic(World& world, size_t index)
        : Type(world, Node_Generic, 0, true)
        , index_(index)
    {}

    virtual size_t hash() const { return hash_combine(Type::hash(), index()); }
    virtual bool equal(const Type* other) const { 
        return Type::equal(other) ? index() == other->as<Generic>()->index() : false; 
    }

public:
    size_t index() const { return index_; }

private:
    size_t index_;

    friend class World;
};

//------------------------------------------------------------------------------

class GenericRef : public Type {
private:
    GenericRef(World& world, const Generic* generic, Lambda* lambda);
    virtual ~GenericRef();

    virtual size_t hash() const { return hash_combine(Type::hash(), lambda()); }
    virtual bool equal(const Type* other) const { 
        return Type::equal(other) ? lambda() == other->as<GenericRef>()->lambda() : false; 
    }

public:
    const Generic* generic() const { return elem(0)->as<Generic>(); }
    Lambda* lambda() const { return lambda_; }

private:
    Lambda* lambda_;

    friend class World;
};

//------------------------------------------------------------------------------

} // namespace thorin

#endif