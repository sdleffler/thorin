#include "thorin/primop.h"
#include "thorin/world.h"
#include "thorin/analyses/cfg.h"
#include "thorin/analyses/domtree.h"
#include "thorin/transform/mangle.h"
#include "thorin/util/hash.h"
#include "thorin/util/queue.h"

#include <iostream>

namespace thorin {

//------------------------------------------------------------------------------

class Call {
public:
    Call() {}
    Call(Lambda* to)
        : to_(to)
        , args_(to->type()->num_args())
    {}

    Lambda* to() const { return to_; }
    ArrayRef<Def> args() const { return args_; }
    Def& arg(size_t i) { return args_[i]; }
    const Def& arg(size_t i) const { return args_[i]; }
    bool operator == (const Call& other) const { return this->to() == other.to() && this->args() == other.args(); }

private:
    Lambda* to_;
    Array<Def> args_;
};

struct CallHash {
    size_t operator () (const Call& call) const {
        return hash_combine(hash_value(call.args()), call.to());
    }
};

//------------------------------------------------------------------------------

class PartialEvaluator {
public:
    PartialEvaluator(World& world)
        : world_(world)
    {}

    World& world() { return world_; }
    void seek();
    void eval(Lambda* begin, Lambda* cur, Lambda* end);
    void rewrite_jump(Lambda* src, Lambda* dst, const Call&);
    void enqueue(Lambda* lambda) { 
        if (!visit(visited_, lambda))
            queue_.push(lambda); 
    }

private:
    World& world_;
    LambdaSet done_;
    std::queue<Lambda*> queue_;
    LambdaSet visited_;
    HashMap<Call, Lambda*, CallHash> cache_;
};

static Lambda* continuation(Lambda* lambda) {
    return lambda->num_args() != 0 ? lambda->args().back()->isa_lambda() : (Lambda*) nullptr;
}

void PartialEvaluator::seek() {
    for (auto lambda : world().externals())
        enqueue(lambda);

    while (!queue_.empty()) {
        auto lambda = pop(queue_);
        if (!lambda->empty()) {
            if (lambda->to()->isa<Run>())
                eval(lambda, lambda, continuation(lambda));
        }

        for (auto succ : lambda->succs())
            enqueue(succ);
    }
}

void PartialEvaluator::eval(Lambda* begin, Lambda* cur, Lambda* end) {
    if (end == nullptr)
        std::cout << "no matching end: " << cur->unique_name() << std::endl;

    while (cur && !done_.contains(cur) && cur != end) {
        if (cur->empty()) {
            std::cout << "bailing out: " << cur->unique_name() << std::endl;
            return;
        }

        //cur->dump_head();

        Lambda* dst = nullptr;
        if (auto run = cur->to()->isa<Run>()) {
            dst = run->def()->isa_lambda();
        } else if (cur->to()->isa<Hlt>()) {
            for (auto succ : cur->succs())
                enqueue(succ);
            begin = cur = continuation(cur);
            continue;
        } else {
            dst = cur->to()->isa_lambda();
        }

        done_.insert(cur);
        if (dst == nullptr) {
            std::cout << "bailing out: " << cur->unique_name() << std::endl;
            return;
        }

        if (dst->empty()) {
            if (dst == world().branch()) {
                std::cout << "-----------------------------------------" << std::endl;
                world().cleanup();
                world().dump();
                std::cout << "-----------------------------------------" << std::endl;
                Scope scope(begin);
                auto& postdomtree = *scope.cfg()->postdomtree();
                //begin = cur = postdomtree.lookup(scope.cfg()->lookup(cur))->idom()->lambda();
                cur = postdomtree.lookup(scope.cfg()->lookup(cur))->idom()->lambda();
                continue;
            } else
                //begin = cur = continuation(cur);
                cur = continuation(cur);
            continue;
        }

        Call call(dst);
        for (size_t i = 0; i != cur->num_args(); ++i) {
            if (!cur->arg(i)->isa<Hlt>())
                call.arg(i) = cur->arg(i);
        }

        if (auto cached = find(cache_, call)) { // check for cached version
            rewrite_jump(cur, cached, call);
            return;
        } else {                                // no cached version found... create a new one
            Scope scope(dst);
            Type2Type type2type;
            bool res = dst->type()->infer_with(type2type, cur->arg_fn_type());
            assert(res);
            auto dropped = drop(scope, call.args(), type2type);
            rewrite_jump(cur, dropped, call);
            cur = dropped;
        }
    }
}

void PartialEvaluator::rewrite_jump(Lambda* src, Lambda* dst, const Call& call) {
    std::vector<Def> nargs;
    for (size_t i = 0, e = src->num_args(); i != e; ++i) {
        if (call.arg(i) == nullptr)
            nargs.push_back(src->arg(i));
    }

    src->jump(dst, nargs);
    cache_[call] = dst;
}

//------------------------------------------------------------------------------

void partial_evaluation(World& world) {
    PartialEvaluator(world).seek();
    std::cout << "PE done" << std::endl;

    for (auto primop : world.primops()) {
        if (auto evalop = Def(primop)->isa<EvalOp>())
            evalop->replace(evalop->def());
    }
}

//------------------------------------------------------------------------------

}
