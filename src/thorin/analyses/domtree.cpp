#include "thorin/analyses/domtree.h"

#include "thorin/util/ycomp.h"

namespace thorin {

template<bool forward>
void DomTreeBase<forward>::create() {
    // map entry's initial idom to itself
    idoms_[cfg().entry()] = cfg().entry();

    // all others' idom are set to their first found dominating pred
    for (auto n : cfg().body()) {
        for (auto pred : cfg().preds(n)) {
            if (cfg().index(pred) < cfg().index(n)) {
                idoms_[n] = pred;
                goto outer_loop;
            }
        }
        THORIN_UNREACHABLE;
outer_loop:;
    }

    for (bool todo = true; todo;) {
        todo = false;

        for (auto n : cfg().body()) {
            const CFNode* new_idom = nullptr;
            for (auto pred : cfg().preds(n))
                new_idom = new_idom ? lca(new_idom, pred) : pred;

            assert(new_idom);
            if (idom(n) != new_idom) {
                idoms_[n] = new_idom;
                todo = true;
            }
        }
    }

    for (auto n : cfg().body())
        children_[idom(n)].push_back(n);
}

template<bool forward>
const CFNode* DomTreeBase<forward>::lca(const CFNode* i, const CFNode* j) const {
    assert(i && j);
    while (index(i) != index(j)) {
        while (index(i) < index(j)) j = idom(j);
        while (index(j) < index(i)) i = idom(i);
    }
    return i;
}

template<bool forward>
void DomTreeBase<forward>::stream_ycomp(std::ostream& out) const {
    thorin::ycomp(out, scope(), range(cfg().rpo()),
        [&] (const CFNode* n) { return range(children(n)); },
        YComp_Orientation::TopToBottom
    );
}

template class DomTreeBase<true>;
template class DomTreeBase<false>;

}
