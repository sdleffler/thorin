#include "thorin/primop.h"
#include "thorin/world.h"
#include "thorin/analyses/scope.h"
#include "thorin/analyses/verify.h"
#include "thorin/util/queue.h"

namespace thorin {

static const Enter* find_enter(Def def) {
    for (auto use : def->uses()) {
        if (auto enter = use->isa<Enter>())
            return enter;
    }
    return nullptr;
}

static void find_enters(Lambda* lambda, std::vector<const Enter*>& enters) {
    if (auto param = lambda->mem_param()) {
        for (Def cur = param; cur;) {
            if (auto enter = find_enter(cur))
                enters.push_back(enter);

            if (auto memop = cur->isa<MemOp>()) {
                if (auto mem_out = memop->mem_out()) {
                    assert(mem_out);
                    cur = mem_out;
                }
            }

            for (auto use : cur->uses()) {
                cur = nullptr;
                if (auto memop = use->isa<MemOp>()) {
                    if (memop->has_mem_out())
                        cur = memop;
                }
            }
        }
    }
}

static void lift_enters(const Scope& scope) {
    World& world = scope.world();
    std::vector<const Enter*> enters;

    for (size_t i = scope.size(); i-- != 1;)
        find_enters(scope.rpo(i), enters);

    auto enter = find_enter(scope.entry());
    if (enter == nullptr)
        enter = world.enter(scope.entry()->mem_param())->as<Enter>();

    // find max slot index
    size_t index = 0;
    for (auto use : enter->uses()) {
        if (auto slot = use->isa<Slot>())
            index = std::max(index, slot->index());
    }

    for (auto old_enter : enters) {
        for (auto use : old_enter->uses()) {
            if (auto slot = use->isa<Slot>())
                slot->replace(world.slot(slot->ptr_type()->referenced_type(), enter, index++, slot->name));
        }
    }

#ifndef NDEBUG
    for (auto old_enter : enters)
        assert(old_enter->num_uses() == 0);
#endif
}

void lift_enters(World& world) {
    world.cleanup();
    Scope::for_each(world, [] (const Scope& scope) { lift_enters(scope); });
    world.cleanup();
    debug_verify(world);
}

}
