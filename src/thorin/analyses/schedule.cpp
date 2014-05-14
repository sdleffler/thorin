#include <algorithm>
#include <iostream>

#include "thorin/lambda.h"
#include "thorin/memop.h"
#include "thorin/primop.h"
#include "thorin/world.h"
#include "thorin/analyses/domtree.h"
#include "thorin/analyses/looptree.h"
#include "thorin/analyses/scope.h"
#include "thorin/util/queue.h"

namespace thorin {

typedef LambdaMap<std::vector<const PrimOp*>> Schedule;

static bool sort_primops(Def def1, Def def2) {
    if (def1->kind() == def2->kind())
        return def1->gid() < def2->gid();

    if (def1->isa<Enter>() || def1->isa<Slot>() || def1->isa<Load>() || def1->isa<Store>())
        return true;

    return def1->gid() < def2->gid();
}

Schedule schedule_early(const Scope& scope) {
    Schedule schedule;
    std::queue<Def> queue;
    DefMap<size_t> num_placed;
    DefSet set;
    auto enqueue = [&] (Def def) {
        if (scope.contains(def))
            queue.push(def);
    };

    for (Lambda* lambda : scope) {
        auto& primops = schedule[lambda];

        for (auto param : lambda->params())
            if (!param->is_proxy())
                enqueue(param);

        while (!queue.empty()) {
            auto def = pop(queue);
            if (auto primop = def->isa<PrimOp>())
                primops.push_back(primop);

            std::vector<Def> todo;
            for (auto use : def->uses()) {
                if (use->isa<Lambda>())
                    continue;
                if (visit(set, use))
                    --num_placed[use];
                else {
                    num_placed[use] = -1;
                    for (auto op : use->ops()) {
                        if (scope.contains(op) && !op->isa_lambda())
                            ++num_placed[use];
                    }
                }
                assert(num_placed[use] != size_t(-1));

                if (num_placed[use] == 0)
                    todo.push_back(use);
            }

            std::stable_sort(todo.begin(), todo.end(), sort_primops);
            for (auto def : todo)
                enqueue(def);
        }
    }

    return schedule;
}

static Schedule schedule_late(const Scope& scope, DefMap<Lambda*> &def2late) {
    DefMap<int> def2num;
    std::vector<Def> zero;

    for (auto def : scope.in_scope()) {
        if (auto primop = def->isa<PrimOp>()) {
            int num = 0;
            for (auto use : primop->uses()) {
                if (scope.contains(use))
                    ++num;
            }
            if (num != 0) // not dead
                def2num[def] = num;
        }
    }

    Schedule schedule;
    const DomTree domtree(scope);
    assert(def2late.empty());

    for (auto i = scope.rbegin(), e = scope.rend(); i != e; ++i) {
        auto cur = *i;

        auto decrease = [&] (Def def) {
            assert(scope.contains(def));
            for (auto op : def->ops()) {
                if (op->isa<PrimOp>() && scope.contains(op)) {
                    assert(def2num.find(op) != def2num.end());
                    if (--def2num[op] == 0)
                        zero.push_back(op);
                    assert(def2num[op] >= 0);
                }
            }
        };

        decrease(cur);
        def2late[cur] = cur;

        bool todo = true;
        do {
            std::vector<const PrimOp*> remove;
            std::stable_sort(zero.begin(), zero.end(), [] (Def def1, Def def2) { return !sort_primops(def1, def2); });

            for (auto z : zero) {
                const PrimOp* primop = z->as<PrimOp>();
                auto& late = def2late[primop];
                assert(late == nullptr);
                late = cur;
                for (auto use : primop->uses()) {
                    if (scope.contains(use))
                        late = domtree.lca(late, def2late[use]);
                }
                schedule[late].push_back(primop);
                remove.push_back(primop);
            }

            if (zero.empty())
                todo = false;
            else
                zero.clear();

            for (auto op : remove)
                decrease(op);
        } while (todo);
    }
    assert(zero.empty());

    for (auto& primops : schedule)
        std::reverse(primops.second.begin(), primops.second.end());

    return schedule;
}

Schedule schedule_late(const Scope& scope) { DefMap<Lambda*> late; return schedule_late(scope, late); }

Schedule schedule_smart(const Scope& scope) {
    Schedule smart;
    const DomTree domtree(scope); // TODO cache domtree across schedule_late
    const LoopTree looptree(scope);
    Schedule early = schedule_early(scope);
    DefMap<Lambda*> def2late;
    schedule_late(scope, def2late); // set late pointers in primop and remember pass

    for (auto lambda_early : scope) {
        for (auto primop : early[lambda_early]) {
            if (!def2late.contains(primop))
                continue;       // primop is dead
            Lambda* lambda_best = def2late[primop];
            assert(scope.contains(lambda_best));
            int depth = std::numeric_limits<int>::max();
            for (Lambda* i = lambda_best; i != lambda_early; i = domtree.idom(i)) {
                int cur_depth = looptree.depth(i);
                if (cur_depth < depth) {
                    lambda_best = i;
                    depth = cur_depth;
                }
            }
            smart[lambda_best].push_back(primop);
        }
    }

    return smart;
}

} // namespace thorin
