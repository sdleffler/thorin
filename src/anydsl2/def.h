#ifndef ANYDSL2_DEF_H
#define ANYDSL2_DEF_H

#include <string>
#include <unordered_set>
#include <vector>

#include "anydsl2/enums.h"
#include "anydsl2/node.h"
#include "anydsl2/util/array.h"
#include "anydsl2/util/autoptr.h"
#include "anydsl2/util/cast.h"

namespace anydsl2 {

//------------------------------------------------------------------------------

class DefNode;
class Lambda;
class PrimOp;
class Sigma;
class Tracker;
class Type;
class World;

//------------------------------------------------------------------------------

class Peek {
public:
    Peek() {}
    Peek(const DefNode* def, Lambda* from)
        : def_(def)
        , from_(from)
    {}

    const DefNode* def() const { return def_; }
    Lambda* from() const { return from_; }

private:
    const DefNode* def_;
    Lambda* from_;
};

typedef Array<Peek> Peeks;

//------------------------------------------------------------------------------

class Def {
public:
    Def() 
        : node_(nullptr)
    {}
    Def(const DefNode* node)
        : node_(node)
    {}

    bool empty() const { return node_ == nullptr; }
    const DefNode* node() const { return node_; }
    const DefNode* deref() const;
    bool operator == (Def other) const { return this->deref() == other.deref(); }
    bool operator != (Def other) const { return this->deref() != other.deref(); }
    operator const DefNode*() const { return deref(); }
    const DefNode* operator -> () const { return deref(); }

private:
    const DefNode* node_;
};

/** 
 * References a user.
 * A \p Def u which uses \p Def d as i^th operand is a \p Use with \p index_ i of \p Def d.
 */
class Use {
public:
    Use() {}
    Use(size_t index, const DefNode* def)
        : index_(index)
        , def_(def)
    {}

    size_t index() const { return index_; }
    const DefNode* def() const { return def_; }
    bool operator == (Use use) const { return def() == use.def() && index() == use.index(); }
    bool operator != (Use use) const { return def() != use.def() || index() != use.index(); }
    bool operator < (Use) const;
    operator const DefNode*() const { return def_; }
    const DefNode* operator -> () const { return def_; }

private:
    size_t index_;
    const DefNode* def_;
};

//------------------------------------------------------------------------------

struct UseHash { size_t operator () (Use use) const { return hash_combine(hash_value(use.def()), use.index()); } };
struct UseEqual { bool operator () (Use use1, Use use2) const { return use1 == use2; } };

typedef std::unordered_set<Use, UseHash, UseEqual> Uses;

//------------------------------------------------------------------------------

/**
 * The base class for all three kinds of Definitions in AnyDSL.
 * These are:
 * - \p PrimOp%s
 * - \p Param%s and
 * - \p Lambda%s.
 */
class DefNode : public Node<DefNode> {
protected:
    DefNode(size_t gid, int kind, size_t size, const Type* type, bool is_const, const std::string& name)
        : Node(kind, size, name)
        , type_(type)
        , uses_(13) // 13 seems to perform best
        , representitive_(this)
        , gid_(gid)
        , is_const_(is_const)
    {}

    void set_type(const Type* type) { type_ = type; }
    void unregister_use(size_t i) const { op(i)->uses_.erase(Use(i, this)); }

public:
    virtual ~DefNode() {}
    void set_op(size_t i, const DefNode* def);
    void unset_op(size_t i);
    void unset_ops();
    Lambda* as_lambda() const;
    Lambda* isa_lambda() const;
    bool is_const() const { return is_const_; }
    /**
     * Returns the maximum depth of this \p Def%s depdency tree (induced by the \p ops).
     * \em const dependences are consideres leaves in this tree.
     * Thus, those dependences are not further propagted to determine the depth.
     */
    int non_const_depth() const;
    void dump() const;
    const PrimOp* is_non_const_primop() const;

    const Uses& uses() const { return uses_; }
    Array<Use> copy_uses() const;
    size_t num_uses() const { return uses_.size(); }
    size_t gid() const { return gid_; }
    std::string unique_name() const;
    const Type* type() const { return type_; }
    int order() const;
    bool is_generic() const;
    World& world() const;
    ArrayRef<const DefNode*> ops() const { return ops_ref<const DefNode*>(); }
    ArrayRef<const DefNode*> ops(size_t begin, size_t end) const { return ops().slice(begin, end); }
    const DefNode* op(size_t i) const { assert(i < ops().size()); return ops()[i]; }
    const DefNode* op_via_lit(const DefNode* def) const;
    void replace(const DefNode*) const;
    /**
     * Returns the vector length.
     * Raises an assertion if type of this is not a \p VectorType.
     */
    size_t length() const;

    bool is_primlit(int val) const;
    bool is_zero() const { return is_primlit(0); }
    bool is_minus_zero() const;
    bool is_one() const { return is_primlit(1); }
    bool is_allset() const { return is_primlit(-1); }
    bool is_div()         const { return anydsl2::is_div  (kind()); }
    bool is_rem()         const { return anydsl2::is_rem  (kind()); }
    bool is_bitop()       const { return anydsl2::is_bitop(kind()); }
    bool is_shift()       const { return anydsl2::is_shift(kind()); }
    bool is_not()         const { return kind() == ArithOp_xor && op(0)->is_allset(); }
    bool is_minus()       const { return (kind() == ArithOp_sub || kind() == ArithOp_fsub) && op(0)->is_minus_zero(); }
    bool is_div_or_rem()  const { return anydsl2::is_div_or_rem(kind()); }
    bool is_commutative() const { return anydsl2::is_commutative(kind()); }
    bool is_associative() const { return anydsl2::is_associative(kind()); }

    // implementation in literal.h
    template<class T> inline T primlit_value() const;

private:
    DefNode& operator = (const DefNode&); /// Do not copy-assign a \p Def instance.

    const Type* type_;
    mutable Uses uses_;
    mutable const DefNode* representitive_;
    const size_t gid_;

protected:
    bool is_const_;

    friend class PrimOp;
    friend class World;
};

std::ostream& operator << (std::ostream& o, const DefNode* def);
inline bool Use::operator < (Use use) const { return def()->gid() < use.def()->gid() && index() < use.index(); }

//------------------------------------------------------------------------------

class Param : public DefNode {
private:
    Param(size_t gid, const Type* type, Lambda* lambda, size_t index, const std::string& name);

public:
    Lambda* lambda() const { return lambda_; }
    size_t index() const { return index_; }
    Peeks peek() const;

private:
    mutable Lambda* lambda_;
    const size_t index_;

    friend class World;
    friend class Lambda;
};

//------------------------------------------------------------------------------

} // namespace anydsl2

#endif
