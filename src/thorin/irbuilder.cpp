#include "thorin/irbuilder.h"

#include "thorin/primop.h"
#include "thorin/world.h"

namespace thorin {

//------------------------------------------------------------------------------

Var Var::create_val(IRBuilder& builder, Def val) {
    Var result;
    result.kind_    = ImmutableValRef;
    result.builder_ = &builder;
    result.def_     = val;
    return result;
}

Var Var::create_mut(IRBuilder& builder, size_t handle, Type type, const char* name) {
    Var result;
    result.kind_    = MutableValRef;
    result.builder_ = &builder;
    result.handle_  = handle;
    result.type_    = *type;
    result.name_    = name;
    return result;
}

Var Var::create_ptr(IRBuilder& builder, Def ptr) {
    Var result;
    result.kind_    = PtrRef;
    result.builder_ = &builder;
    result.def_     = ptr;
    return result;
}

Var Var::create_agg(Var var, Def offset) {
    assert(var.kind() != Empty);
    if (var.kind() == PtrRef)
        return create_ptr(*var.builder_, var.builder_->world().lea(var.def_, offset));
    Var result;
    result.kind_    = AggRef;
    result.builder_ = var.builder_;
    result.var_.reset(new Var(var));
    result.def_     = offset;
    return result;
}

Def Var::load() const {
    switch (kind()) {
        case ImmutableValRef: return def_;
        case MutableValRef:   return builder_->cur_bb->get_value(handle_, Type(type_), name_);
        case PtrRef:          return builder_->load(def_);
        case AggRef:          return builder_->extract(var_->load(), def_);
        default: THORIN_UNREACHABLE;
    }
}

void Var::store(Def val) const {
    switch (kind()) {
        case MutableValRef: builder_->cur_bb->set_value(handle_, val); return;
        case PtrRef:        builder_->store(def_, val); return;
        case AggRef:        var_->store(world().insert(var_->load(), def_, val)); return;
        default: THORIN_UNREACHABLE;
    }
}

World& Var::world() const { return builder_->world(); }

//------------------------------------------------------------------------------

#ifndef NDEBUG
JumpTarget::~JumpTarget() { assert((!lambda_ || first_ || lambda_->is_sealed()) && "JumpTarget not sealed"); }
#endif

World& JumpTarget::world() const { assert(lambda_); return lambda_->world(); }
void JumpTarget::seal() { assert(lambda_); lambda_->seal(); }

Lambda* JumpTarget::untangle() {
    if (!first_)
        return lambda_;
    assert(lambda_);
    auto bb = world().basicblock(name_);
    lambda_->jump(bb, {});
    first_ = false;
    return lambda_ = bb;
}

void Lambda::jump(JumpTarget& jt) {
    if (!jt.lambda_) {
        jt.lambda_ = this;
        jt.first_ = true;
    } else
        this->jump(jt.untangle(), {});
}

Lambda* JumpTarget::branch_to(World& world) {
    auto bb = world.basicblock(lambda_ ? name_ + std::string(".crit") : name_);
    bb->jump(*this);
    bb->seal();
    return bb;
}

Lambda* JumpTarget::enter() {
    if (lambda_ && !first_)
        lambda_->seal();
    return lambda_;
}

Lambda* JumpTarget::enter_unsealed(World& world) {
    return lambda_ ? untangle() : lambda_ = world.basicblock(name_);
}

//------------------------------------------------------------------------------

Def IRBuilder::create_frame() {
    auto enter = world().enter(get_mem());
    set_mem(world().extract(enter, 0));
    return world().extract(enter, 1);
}

Def IRBuilder::alloc(Type type, Def extra, const std::string& name) {
    if (!extra)
        extra = world().literal_qu64(0);
    auto alloc = world().alloc(type, get_mem(), extra, name);
    set_mem(world().extract(alloc, 0));
    return world().extract(alloc, 1);
}

Def IRBuilder::load(Def ptr, const std::string& name) {
    auto load = world().load(get_mem(), ptr, name);
    set_mem(world().extract(load, 0));
    return world().extract(load, 1);
}

void IRBuilder::store(Def ptr, Def val, const std::string& name) {
    set_mem(world().store(get_mem(), ptr, val, name));
}

Def IRBuilder::extract(Def agg, u32 index, const std::string& name) {
    return extract(agg, world().literal_qu32(index), name);
}

Def IRBuilder::extract(Def agg, Def index, const std::string& name) {
    if (auto ld = Load::is_out_val(agg))
        return load(world().lea(ld->ptr(), index, ld->name));
    return world().extract(agg, index, name);
}

Lambda* IRBuilder::jump(JumpTarget& jt) {
    if (is_reachable()) {
        cur_bb->jump(jt);
        auto res = cur_bb;
        set_unreachable();
        return res;
    }
    return nullptr;
}

Lambda* IRBuilder::branch(Def cond, JumpTarget& t, JumpTarget& f) {
    if (is_reachable()) {
        if (auto lit = cond->isa<PrimLit>()) {
            return jump(lit->value().get_bool() ? t : f);
        } else if (&t == &f) {
            return jump(t);
        } else {
            auto tl = t.branch_to(world_);
            auto fl = f.branch_to(world_);
            auto res = cur_bb;
            cur_bb->branch(cond, tl, fl);
            set_unreachable();
            return res;
        }
    }
    return nullptr;
}

Lambda* IRBuilder::branch(Def cond, JumpTarget& t, JumpTarget& f, JumpTarget& x) {
    if (is_reachable()) {
        if (auto lit = cond->isa<PrimLit>()) {
            return jump(lit->value().get_bool() ? t : f);
        } else if (&t == &f) {
            return jump(t);
        } else {
            auto tl = t.branch_to(world_);
            auto fl = f.branch_to(world_);
            assert(x.lambda_ == nullptr || !x.first_);
            if (!x.lambda_) {
                x.lambda_ = world().basicblock(x.name_);
                x.first_ = false;
            }
            auto xl = x.lambda_;
            auto res = cur_bb;
            cur_bb->branch_join(cond, tl, fl, xl);
            set_unreachable();
            return res;
        }
    }
    return nullptr;
}

Def IRBuilder::call(Def to, ArrayRef<Def> args, Type ret_type) {
    if (is_reachable()) {
        auto p = cur_bb->call(to, args, ret_type);
        cur_bb = p.first;
        return p.second;
    }
    return Def();
}

Def IRBuilder::get_mem() { return cur_bb->get_mem(); }
void IRBuilder::set_mem(Def mem) { if (is_reachable()) cur_bb->set_mem(mem); }

//------------------------------------------------------------------------------

}
