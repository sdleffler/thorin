#include "thorin/continuation.h"
#include "thorin/world.h"
#include "thorin/analyses/cfg.h"
#include "thorin/analyses/scope.h"
#include "thorin/analyses/verify.h"
#include "thorin/transform/mangle.h"
#include "thorin/util/log.h"

namespace thorin {

void force_inline(Scope& scope, int threshold) {
    for (bool todo = true; todo && threshold-- != 0;) {
        todo = false;
        for (auto n : scope.f_cfg().post_order()) {
            auto continuation = n->continuation();
            if (auto callee = continuation->callee()->isa_continuation()) {
                if (!callee->empty() && !scope.contains(callee)) {
                    Scope callee_scope(callee);
                    continuation->jump(drop(callee_scope, continuation->args()), {}, continuation->jump_debug());
                    todo = true;
                }
            }
        }

        if (todo)
            scope.update();
    }

    for (auto n : scope.f_cfg().reverse_post_order()) {
        auto continuation = n->continuation();
        if (auto callee = continuation->callee()->isa_continuation()) {
            if (!callee->empty() && !scope.contains(callee))
                WLOG(callee, "couldn't inline {} at {}", scope.entry(), continuation->jump_location());
        }
    }
}

void inliner(World& world) {
    VLOG("start inliner");

    static const int factor = 4;
    static const int offset = 4;

    ContinuationMap<std::unique_ptr<Scope>> continuation2scope;

    auto get_scope = [&] (Continuation* continuation) -> Scope* {
        auto i = continuation2scope.find(continuation);
        if (i == continuation2scope.end())
            i = continuation2scope.emplace(continuation, std::make_unique<Scope>(continuation)).first;
        return i->second.get();
    };

    auto is_candidate = [&] (Continuation* continuation) -> Scope* {
        if (!continuation->empty() && continuation->order() > 1) {
            auto scope = get_scope(continuation);
            if (scope->defs().size() < scope->entry()->num_params() * factor + offset)
                return scope;
        }
        return nullptr;
    };

    Scope::for_each(world, [&] (Scope& scope) {
        bool dirty = false;
        for (auto n : scope.f_cfg().post_order()) {
            auto continuation = n->continuation();
            if (auto callee = continuation->callee()->isa_continuation()) {
                if (callee == scope.entry())
                    continue; // don't inline recursive calls
                DLOG("callee: {}", callee);
                if (auto callee_scope = is_candidate(callee)) {
                    DLOG("- here: {}", continuation);
                    continuation->jump(drop(*callee_scope, continuation->args()), {}, continuation->jump_debug());
                    dirty = true;
                }
            }
        }

        if (dirty) {
            scope.update();

            if (auto s = get_scope(scope.entry()))
                s->update();
        }
    });

    VLOG("stop inliner");
    debug_verify(world);
    world.cleanup();
}

}
