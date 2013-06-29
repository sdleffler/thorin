#include "anydsl2/analyses/scope.h"

#include <algorithm>

#include "anydsl2/lambda.h"
#include "anydsl2/world.h"
#include "anydsl2/analyses/domtree.h"
#include "anydsl2/analyses/looptree.h"
#include "anydsl2/util/for_all.h"

namespace anydsl2 {

struct ScopeLess : public std::binary_function<const Lambda*, const Lambda*, bool> {
    bool operator () (const Lambda* l1, const Lambda* l2) const { return l1->sid() < l2->sid(); }
};

struct ScopeLessBackwards : public std::binary_function<const Lambda*, const Lambda*, bool> {
    bool operator () (const Lambda* l1, const Lambda* l2) const { return l1->backwards_sid() < l2->backwards_sid(); }
};

Scope::Scope(Lambda* entry)
    : world_(entry->world())
    , num_entries_(1)
    , num_exits_(-1)
{
    Lambda* entries[1] = { entry };
    identify_scope(entries);
    rpo_numbering(entries);
}

Scope::Scope(World& world, ArrayRef<Lambda*> entries)
    : world_(world)
    , num_entries_(entries.size())
{
    identify_scope(entries);
    rpo_numbering(entries);
}

Scope::Scope(World& world) 
    : world_(world)
    , num_entries_(0)
{
    size_t pass = world.new_pass();

    for_all (lambda, world.lambdas()) {
        if (!lambda->is_visited(pass))
            jump_to_param_users(pass, lambda, lambda);
    }

    std::vector<Lambda*> entries;

    for_all (lambda, world.lambdas()) {
        if (!lambda->is_visited(pass)) {
            insert(pass, lambda);
            entries.push_back(lambda);
        }
    }

    num_entries_ = entries.size();
    rpo_numbering(entries);
}

Scope::~Scope() {
    for_all (lambda, rpo_)
        lambda->scope_ = 0;
}

void Scope::identify_scope(ArrayRef<Lambda*> entries) {
    // identify all lambdas depending on entry
    size_t pass = world().new_pass();
    for_all (entry, entries) {
        insert(pass, entry);
        jump_to_param_users(pass, entry, 0);
    }
}

void Scope::jump_to_param_users(const size_t pass, Lambda* lambda, Lambda* limit) {
    for_all (param, lambda->params())
        find_user(pass, param, limit);
}

inline void Scope::find_user(const size_t pass, const Def* def, Lambda* limit) {
    if (Lambda* lambda = def->isa_lambda())
        up(pass, lambda, limit);
    else {
        if (def->visit(pass))
            return;

        for_all (use, def->uses())
            find_user(pass, use, limit);
    }
}

void Scope::up(const size_t pass, Lambda* lambda, Lambda* limit) {
    if (lambda->is_visited(pass) || (limit && limit == lambda))
        return;

    insert(pass, lambda);
    jump_to_param_users(pass, lambda, limit);

    for_all (pred, lambda->preds())
        up(pass, pred, limit);
}

void Scope::rpo_numbering(ArrayRef<Lambda*> entries) {
    size_t pass = world().new_pass();

    for_all (entry, entries)
        entry->visit_first(pass);

    size_t num = 0;
    for_all (entry, entries)
        num = po_visit<true>(pass, entry, num);

    for (size_t i = entries.size(); i-- != 0;)
        entries[i]->sid_ = num++;

    assert(num <= size());
    assert(num >= 0);

    // convert postorder number to reverse postorder number
    for_all (lambda, rpo()) {
        if (lambda->is_visited(pass)) {
            lambda->sid_ = num - 1 - lambda->sid_;
        } else { // lambda is unreachable
            lambda->scope_ = 0;
            lambda->sid_ = size_t(-1);
        }
    }
    
    // sort rpo_ according to sid_ which now holds the rpo number
    std::sort(rpo_.begin(), rpo_.end(), ScopeLess());

    // discard unreachable lambdas
    rpo_.resize(num);
}

template<bool forwards>
size_t Scope::po_visit(const size_t pass, Lambda* cur, size_t i) const {
    for_all (succ, forwards ? cur->succs() : cur->preds()) {
        if (contains(succ) && !succ->is_visited(pass))
            i = number<forwards>(pass, succ, i);
    }
    return i;
}

template<bool forwards>
size_t Scope::number(const size_t pass, Lambda* cur, size_t i) const {
    cur->visit_first(pass);
    i = po_visit<forwards>(pass, cur, i);
    return forwards ? (cur->sid_ = i) + 1 : (cur->backwards_sid_ = i) - 1;
}

#define ANYDSL2_SCOPE_SUCC_PRED(succ) \
ArrayRef<Lambda*> Scope::succ##s(Lambda* lambda) const { \
    assert(contains(lambda));  \
    if (succ##s_.data() == 0) { \
        succ##s_.alloc(size()); \
        for_all (lambda, rpo_) { \
            Lambdas all_succ##s = lambda->succ##s(); \
            Array<Lambda*>& succ##s = succ##s_[lambda->sid()]; \
            succ##s.alloc(all_succ##s.size()); \
            size_t i = 0; \
            for_all (succ, all_succ##s) { \
                if (contains(succ)) \
                    succ##s[i++] = succ; \
            } \
            succ##s.shrink(i); \
        } \
    } \
    return succ##s_[lambda->sid()];  \
}

ANYDSL2_SCOPE_SUCC_PRED(succ)
ANYDSL2_SCOPE_SUCC_PRED(pred)

ArrayRef<Lambda*> Scope::backwards_rpo() const {
    if (!backwards_rpo_) {
        backwards_rpo_ = new Array<Lambda*>(size());

        std::vector<Lambda*> exits;

        for_all (lambda, rpo()) {
            if (num_succs(lambda) == 0)
                exits.push_back(lambda);
        }

        num_exits_ = exits.size();

        // number all lambdas in postorder
        size_t pass = world().new_pass();

        size_t num = 0;
        for_all (exit, exits) {
            exit->visit_first(pass);
            exit->backwards_sid_ = num++;
        }

        num = size() - 1;
        for_all (exit, exits)
            num = po_visit<false>(pass, exit, num);

        assert(num + 1 == num_exits());

        std::copy(rpo_.begin(), rpo_.end(), backwards_rpo_->begin());
        std::sort(backwards_rpo_->begin(), backwards_rpo_->end(), ScopeLessBackwards());
    }

    return *backwards_rpo_;
}

//------------------------------------------------------------------------------

const DomTree& Scope::domtree() const { return domtree_ ? *domtree_ : *(domtree_ = new DomTree(*this)); }
const PostDomTree& Scope::postdomtree() const { return postdomtree_ ? *postdomtree_ : *(postdomtree_ = new PostDomTree(*this)); }
const LoopTreeNode* Scope::looptree() const { return looptree_ ? looptree_ : looptree_ = create_loop_forest(*this); }
const LoopInfo& Scope::loopinfo() const { return loopinfo_ ? *loopinfo_ : *(loopinfo_ = new LoopInfo(*this)); }

//------------------------------------------------------------------------------

} // namespace anydsl2
