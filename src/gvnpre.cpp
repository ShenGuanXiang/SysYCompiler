#include "gvnpre.h"
#include "SimplifyCFG.h"
#include <queue>
#include <sstream>
#include <map>

static void logf(const char *formmat, ...)
{
#ifdef DEBUG_GVNPRE

    va_list args;
    va_start(args, formmat);
    vfprintf(stderr, formmat, args);
    va_end(args);
#endif
}
static void printset(Exprset set)
{
#ifdef DEBUG_GVNPRE

    if (set.getExprs().empty())
        logf("empty\n");
    else
    {
        for (auto e : set.getExprs())
        {
            logf("(%s:%s)\t", lookup(e)->toStr().c_str(), e.tostr().c_str());
        }
        logf("\n");
    }

#endif
}

static unsigned getInstOp(Instruction *inst)
{
    switch (inst->getInstType())
    {
    case Instruction::BINARY:
    {
        auto bin_inst = dynamic_cast<BinaryInstruction *>(inst);
        switch (bin_inst->getOpcode())
        {
        case BinaryInstruction::ADD:
            return ExprOp::ADD;
        case BinaryInstruction::SUB:
            return ExprOp::SUB;
        case BinaryInstruction::MUL:
            return ExprOp::MUL;
        case BinaryInstruction::DIV:
            return ExprOp::DIV;
        case BinaryInstruction::MOD:
            return ExprOp::MOD;
        default:
            assert(0);
        }
    }
    case Instruction::GEP:
        return ExprOp::GEP;
    case Instruction::PHI:
        return ExprOp::PHI;
    default:
        assert(0);
    }
}

Instruction *GVNPRE_FUNC::genInst(Operand *dst, unsigned op, std::vector<Operand *> leaders)
{
    Instruction *inst;
    switch (op)
    {
    case ExprOp::ADD:
        inst = new BinaryInstruction(BinaryInstruction::ADD, dst, leaders[0], leaders[1]);
        break;
    case ExprOp::SUB:
        inst = new BinaryInstruction(BinaryInstruction::SUB, dst, leaders[0], leaders[1]);
        break;
    case ExprOp::MUL:
        inst = new BinaryInstruction(BinaryInstruction::MUL, dst, leaders[0], leaders[1]);
        break;
    case ExprOp::DIV:
        inst = new BinaryInstruction(BinaryInstruction::DIV, dst, leaders[0], leaders[1]);
        break;
    case ExprOp::MOD:
        inst = new BinaryInstruction(BinaryInstruction::MOD, dst, leaders[0], leaders[1]);
        break;
    case ExprOp::GEP:
    {
        std::vector<Operand *> idxs;
        std::copy(leaders.begin() + 1, leaders.end(), std::back_inserter(idxs));
        inst = new GepInstruction(dst, leaders[0], idxs);
        break;
    }
    default:
        assert(0);
    }
    return inst;
}

Instruction *GVNPRE_FUNC::genInst(Operand *dst, unsigned op, std::map<BasicBlock *, Operand *> phiargs)
{
    PhiInstruction *inst = new PhiInstruction(dst, false);
    for (auto pair : phiargs)
    {
        auto bb = pair.first;
        auto src = pair.second;
        inst->addEdge(bb, src);
    }
    return (Instruction *)inst;
}

Exprset GVNPRE_FUNC::phi_trans(Exprset set, BasicBlock *from, BasicBlock *to)
{
    // convert set to array in topological order
    const std::vector<Expr> &toplist = set.topological_sort();
    Exprset newset;
    // translate each expression in order
    // fprintf(stderr, "pre-compute\n");

    std::unordered_map<ValueNr, ValueNr> &cur_cache = trans_cache[{from->getNo(), to->getNo()}];
    for (size_t i = 0; i < toplist.size(); i++)
    {
        Expr e = toplist[i];
        if (e.getOpcode() == ExprOp::TEMP) // tempraray
        {
            Operand *oldval = lookup(e);
            Instruction *definst = oldval->getDef();
            if (definst && definst->getInstType() == Instruction::PHI && definst->getParent() == to)
            {
                Operand *newtemp = dynamic_cast<PhiInstruction *>(definst)->getSrcs()[from];
                Expr newexpr(newtemp);
                // ValueNr newval = lookup(e);
                ValueNr newval = lookup(newexpr);
                cur_cache[oldval] = newval;
                newset.insert(newexpr);
            }
            else
            {
                newset.insert(e);
            }
        }
        else
        { // expression
            ValueNr oldval = lookup(e);
            std::vector<ValueNr> newvals;
            for (auto tmp : e.getOperands())
            {
                ValueNr val = lookup(tmp);
                if (cur_cache.count(val))
                    newvals.push_back(cur_cache[val]);
                else
                {
                    newvals.push_back(val);
                }
            }
            Expr newexpr(e.getOpcode(), newvals);
            if (e != newexpr)
            {
                // e's composite val is changed now, e is not ever the old e
                if (!htable.count(newexpr)) // modify htable in the process (insert new mapping)
                {
                    Operand *tmp = new Operand(new TemporarySymbolEntry(oldval->getType(), SymbolTable::getLabel()));
                    htable[tmp] = tmp;
                    htable[newexpr] = tmp;
                }
                ValueNr newval = lookup(newexpr);
                cur_cache[oldval] = newval;
            }
            newset.insert(newexpr);
        }
    }
    return newset;
}

Expr GVNPRE_FUNC::phi_trans(Expr expr, BasicBlock *from, BasicBlock *to)
{
    std::unordered_map<ValueNr, ValueNr> &cur_cache = trans_cache[{from->getNo(), to->getNo()}];
    // add tag to further speedup
    // fprintf(stderr, "%d-%d", from->getNo(), to->getNo());
    if (cur_cache.empty())
        phi_trans(antic_in[to], from, to);

    std::vector<ValueNr> newvals;
    for (auto tmp : expr.getOperands())
    {
        ValueNr val = lookup(tmp);
        if (cur_cache.count(val))
            val = cur_cache[val];
        newvals.push_back(val);
    }
    return Expr(expr.getOpcode(), newvals);

    //-------------old implementation----------------
    // expr is bound to defined in bb'to'
    const std::vector<Expr> &toplist = antic_in[to].topological_sort();
    size_t epos = 0;
    for (; epos < toplist.size(); epos++)
        if (toplist[epos] == expr)
            break;
    if (epos == toplist.size())
        return expr;

    // translate each expression in order
    std::unordered_map<Operand *, Operand *> trans_cache;
    for (size_t i = 0; i <= epos; i++)
    {
        Expr e = toplist[i];
        if (e.getOpcode() == ExprOp::TEMP) // tempraray
        {
            Operand *oldval = lookup(e);
            Instruction *definst = oldval->getDef();
            if (definst && definst->getInstType() == Instruction::PHI && definst->getParent() == to)
            {
                Operand *newtemp = dynamic_cast<PhiInstruction *>(definst)->getSrcs()[from];
                Expr newexpr(newtemp);
                ValueNr newval = lookup(newexpr);
                trans_cache[oldval] = newval;
                // logf("mapping %s to %s\n",oldval->toStr().c_str(),newval->toStr().c_str());
                if (i == epos)
                    return newexpr;
            }
        }
        else
        { // expression
            ValueNr oldval = lookup(e);
            std::vector<ValueNr> newvals;
            for (auto tmp : e.getOperands())
            {
                ValueNr val = lookup(tmp);
                if (trans_cache.count(val))
                    val = trans_cache[val];
                newvals.push_back(val);
            }
            Expr newexpr(e.getOpcode(), newvals);
            if (e != newexpr)
            {
                // e's composite val is changed now, e is not ever the old e
                if (!htable.count(newexpr)) // modify htable in the process (insert new mapping)
                {
                    Operand *tmp = new Operand(new TemporarySymbolEntry(oldval->getType(), SymbolTable::getLabel()));
                    htable[tmp] = tmp;
                    htable[newexpr] = tmp;
                    // htable[newexpr] = new Operand(new TemporarySymbolEntry(oldval->getType(), SymbolTable::getLabel()));
                }
                ValueNr newval = lookup(newexpr);
                trans_cache[oldval] = newval;
                // logf("mapping %s to %s\n",oldval->toStr().c_str(),newval->toStr().c_str());
                if (i == epos)
                    return newexpr;
            }
        }
    }
    return expr;
}

void GVNPRE_FUNC::clean(Exprset &set)
{
    // it seems that we don't need maintain 'kill' set ?
    // logf("before cleaning:\n");
    // printset(set);
    std::vector<Expr> toplist = set.topological_sort();
    auto &valset = set.getValnrs();
    for (const Expr &e : toplist)
    {
        assert(lookup(e));
        if (e.getOpcode() == ExprOp::TEMP) // tempraray
            continue;
        for (auto item : e.getOperands())
        {
            if (item->getEntry()->isConstant() || item->getEntry()->isVariable())
                continue;
            if (!valset.count(lookup(item)))
            {
                set.erase(e);
                set.getValnrs().erase(lookup(e));
            }
        }
    }
    // logf("after cleaning:\n");
    // TODO : could the dag be separated into several parts?
}

Operand *GVNPRE_FUNC::gen_fresh_tmep(Expr e)
{
    // gen new temp as e's type
    ValueNr val = lookup(e);
    assert(val);
    return new Operand(new TemporarySymbolEntry(val->getEntry()->getType(), SymbolTable::getLabel()));
}

void GVNPRE_FUNC::rmcEdge(Function *func)
{
    // remove critical edge with function
    // bfs edge in cfg
    std::vector<std::pair<BasicBlock *, BasicBlock *>> cedges;
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (bb->getNumOfSucc() > 1)
        {
            for (auto succ_it = bb->succ_begin(); succ_it != bb->succ_end(); succ_it++)
            {
                BasicBlock *succ = *succ_it;
                if (succ->getNumOfPred() > 1)
                {
                    cedges.push_back(std::make_pair(bb, succ));
                }
            }
        }
    }
    for (auto e : cedges)
    {
        auto from = e.first;
        auto to = e.second;
        auto newbb = new BasicBlock(func);
        new UncondBrInstruction(to, newbb);
        from->removeSucc(to);
        to->removePred(from);
        from->addSucc(newbb);
        newbb->addPred(from);
        to->addPred(newbb);
        newbb->addSucc(to);
        // modify branch in from
        for (auto inst = from->rbegin(); inst != from->rend(); inst = inst->getPrev())
        {
            if (inst->isCond())
            {
                auto br = dynamic_cast<CondBrInstruction *>(inst);
                if (br->getTrueBranch() == to)
                {
                    br->setTrueBranch(newbb);
                }
                if (br->getFalseBranch() == to)
                {
                    br->setFalseBranch(newbb);
                }
            }
            else if (inst->isUncond())
            {
                auto br = dynamic_cast<UncondBrInstruction *>(inst);
                if (br->getBranch() == to)
                {
                    br->setBranch(newbb);
                }
            }
            else
                break;
        }
        // modify phi in to
        for (auto inst = to->begin(); inst != to->end(); inst = inst->getNext())
        {
            if (inst->isPHI())
            {
                auto phi = dynamic_cast<PhiInstruction *>(inst);
                auto &src = phi->getSrcs();
                if (src.count(from))
                {
                    src[newbb] = src[from];
                    src.erase(from);
                }
            }
            else
                break;
        }
        logf("insert bb%d between (%d,%d)\n", newbb->getNo(), from->getNo(), to->getNo());
    }
}

void GVNPRE_FUNC::addGval(Function *func)
{
    // htable for each function, so clear it
    //  add global value to htable
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            if (inst->isPHI())
            {
                for (auto p : dynamic_cast<PhiInstruction *>(inst)->getSrcs())
                {
                    const auto &symentry = p.second->getEntry();
                    if (symentry->isConstant() || symentry->isVariable())
                        htable[p.second] = p.second;
                }
            }
            else
            {
                for (auto operand : inst->getUses())
                {
                    const auto &symentry = operand->getEntry();
                    if (symentry->isConstant() || symentry->isVariable())
                        htable[operand] = operand;
                }
            }
        }
    }
}

void GVNPRE_FUNC::buildDomTree(Function *func)
{
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (!bb->getIDom() || bb == func->getEntry())
            continue;
        domtree[bb->getIDom()].push_back(bb);
    }
    // print domtree
    logf("dom:\n");
    for (auto it = domtree.begin(); it != domtree.end(); it++)
    {
        BasicBlock *bb = it->first;
        logf("bb%d:", bb->getNo());
        for (auto succ : it->second)
            logf("bb%d ", succ->getNo());
        logf("\n");
    }
}

void GVNPRE_FUNC::buildSets(Function *func)
{
    // bfs domtree,from entry
    std::queue<BasicBlock *> q;
    std::set<BasicBlock *> visited;
    q.push(func->getEntry());
    while (!q.empty())
    {
        BasicBlock *bb = q.front();
        q.pop();
        if (visited.count(bb))
            continue;
        visited.insert(bb);

        BasicBlock *dom = bb->getIDom();
        if (avail_out.count(dom))
            avail_out[bb] = avail_out[dom];
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            if (inst->hasNoDef())
                continue;

            // inst->output();

            Operand *dst = inst->getDef();

            if (inst->isPHI())
            {
                Expr e(dst);
                htable[e] = dst;
                phi_gen[bb].insert(e);
            }
            else if (inst->isBinary() || inst->isGep())
            {
                const std::vector<Operand *> &temps = inst->getUses();
                std::vector<Operand *> valnrs;
                for (Operand *temp : temps)
                {
                    if (htable.find(temp) == htable.end())
                    {
                        // const auto &symentry = temp->getEntry();
                        // global, parameter, constant is available anywhere instinctly
                        // fprintf(stderr, "%s\n", symentry->toStr().c_str());
                        // assert(symentry->isConstant() || symentry->isVariable());
                        // add a new value number
                        htable[temp] = temp;
                    }
                    valnrs.push_back(htable[temp]);
                }
                Expr e(getInstOp(inst), valnrs);
                if (htable.find(e) == htable.end())
                {
                    htable[e] = dst;
                    if (e.getOpcode() == ExprOp::ADD || e.getOpcode() == ExprOp::MUL)
                        htable[Expr(e.getOpcode(), {e.getOperands()[1], e.getOperands()[0]})] = dst;
                }
                htable[dst] = htable[e];
                for (Operand *operand : temps)
                {
                    expr_gen[bb].vinsert(htable[operand]);
                }
                expr_gen[bb].vinsert(e);
                tmp_gen[bb].insert(dst);
            }
            else
            {
                htable[dst] = dst;
                tmp_gen[bb].insert(dst);
            }
            avail_out[bb].vinsert(dst);
        }

        if (domtree.count(bb))
            for (auto succ_it = domtree[bb].rbegin(); succ_it != domtree[bb].rend(); succ_it++)
                q.push(*succ_it);
    }
}

void GVNPRE_FUNC::buildAntic(Function *func)
{
    bool changed = true;
    int iter = 0;
    while (changed)
    {
        changed = false;
        iter++;
        // std::cout << "[gvn]iter:" << iter << "\n";
        // TODO : traverse postdominator tree
        std::queue<BasicBlock *> q;
        std::set<BasicBlock *> visited;
        q.push(func->getEntry());
        while (!q.empty())
        {
            BasicBlock *bb = q.front();
            q.pop();
            if (visited.count(bb))
                continue;
            visited.insert(bb);
            // logf("iter %d: compute bb%d\n", iter, bb->getNo());

            Exprset _antic_out;
            Exprset old = antic_in[bb];
            if (bb->getNumOfSucc() == 1)
            {
                auto theSucc = *bb->succ_begin();
                _antic_out = phi_trans(antic_in[theSucc], bb, theSucc);
            }
            else if (bb->getNumOfSucc() > 1)
            {
                auto succ_it = bb->succ_begin();
                auto first = *succ_it;
                _antic_out = antic_in[first];

                for (succ_it++; succ_it != bb->succ_end(); succ_it++)
                {
                    auto succ = *succ_it;
                    std::vector<Expr> toerase;
                    for (const auto &e : _antic_out)
                    {
                        ValueNr val = lookup(e);
                        if (!antic_in[succ].find_leader(val))
                            toerase.push_back(e);
                    }
                    for (auto e : toerase)
                        _antic_out.erase(e);
                }
            }
            Exprset S;
            antic_in[bb].clear();
            std::set_difference(_antic_out.getExprs().begin(), _antic_out.getExprs().end(), tmp_gen[bb].getExprs().begin(), tmp_gen[bb].getExprs().end(), std::inserter(S.getExprs(), S.getExprs().end()));
            std::set_difference(expr_gen[bb].getExprs().begin(), expr_gen[bb].getExprs().end(), tmp_gen[bb].getExprs().begin(), tmp_gen[bb].getExprs().end(), std::inserter(antic_in[bb].getExprs(), antic_in[bb].end()));
            for (const auto &e : antic_in[bb])
            {
                antic_in[bb].getValnrs()[lookup(e)].insert(e);
            }
            // logf("antic_in[bb%d]:\n", bb->getNo());
            // printset(antic_in[bb]);
            // logf("S:\n");
            // printset(_antic_out);

            for (const auto &e : S)
            {
                if (!antic_in[bb].find_leader(lookup(e)))
                    antic_in[bb].vinsert(e);
            }

            clean(antic_in[bb]);
            // logf("after clean:\nantic_in[bb%d]:\n", bb->getNo());
            // printset(antic_in[bb]);
            // logf("\n");

            if (old != antic_in[bb])
                changed = true;
            if (domtree.count(bb))
                for (auto succ_it = domtree[bb].rbegin(); succ_it != domtree[bb].rend(); succ_it++)
                    q.push(*succ_it);
        }
    }
    // fprintf(stderr, "[GVNPRE]:build antic iterate over %d times\n", iter);
}

void GVNPRE_FUNC::insert(Function *func)
{
    bool new_stuff = true;
    int iter = 0;
    while (new_stuff)
    {
        iter++;
        new_stuff = false;
        logf("new iteration\n");

        std::queue<BasicBlock *> q;
        std::set<BasicBlock *> visited;
        q.push(func->getEntry());

        while (!q.empty())
        {
            BasicBlock *bb = q.front();
            q.pop();
            if (visited.count(bb))
                continue;
            visited.insert(bb);
            // new_sets[bb].clear();
            // TODO: find good time to clear new_sets
            BasicBlock *dom = bb->getIDom();

            // if(dom){
            //     // new_sets[bb].clear();
            //     logf("bb%d inherit from bb%d\n",bb->getNo(), dom->getNo());
            //     for(const auto& e : new_sets[dom]){
            //         new_sets[bb].vinsert(e);
            //         avail_out[bb].vrplc(e);
            //     }
            // }

            if (bb->getNumOfPred() > 1)
            {
                std::vector<Expr> toplist = antic_in[bb].topological_sort();
                for (const auto &e : toplist)
                {
                    if (e.getOpcode() != ExprOp::TEMP)
                    { // v1 op v2 etc.
                        if (avail_out[dom].find_leader(lookup(e)))
                            continue;

                        logf("%s\n", e.tostr().c_str());

                        std::unordered_map<BasicBlock *, Expr> avail;
                        bool by_some = false, all_same = true;
                        Expr *first_s = nullptr;
                        for (auto pred_it = bb->pred_begin(); pred_it != bb->pred_end(); pred_it++)
                        {
                            auto pred = *pred_it;
                            // TODO: optimize 'phi_trans' to avoid calulating the whole set
                            Expr trans_e = phi_trans(e, pred, bb);
                            logf("%s->%s\n", e.tostr().c_str(), trans_e.tostr().c_str());
                            ValueNr trans_val = lookup(trans_e);
                            if (Operand *temp_leader = avail_out[pred].find_leader(trans_val))
                            {
                                Expr leader(temp_leader);
                                avail[pred] = leader;
                                by_some = true;
                                if (!first_s)
                                    first_s = new Expr(leader);
                                else if (*first_s != leader)
                                    all_same = false;
                            }
                            else
                            {
                                avail[pred] = trans_e;
                                all_same = false;
                            }
                        }
                        if (first_s)
                            delete first_s;

                        if (!all_same && by_some)
                        {
                            logf("inserting:%s in bb%d\n", e.tostr().c_str(), bb->getNo());
                            for (auto item : avail)
                                logf("bb%d: %s\n", item.first->getNo(), item.second.tostr().c_str());

                            bool insert_phi = false;
                            for (auto pred_it = bb->pred_begin(); pred_it != bb->pred_end(); pred_it++)
                            {
                                auto pred = *pred_it;
                                Expr avail_e = avail[pred];
                                if (avail_e.getOpcode() != ExprOp::TEMP && avail_e.getOpcode() != ExprOp::PHI)
                                {
                                    insert_phi = true;
                                    // logf("copy %s from bb%d to bb%d\n", avail_e.c_str(), bb->getNo(), pred->getNo());
                                    // new_stuff = true;
                                    std::vector<Operand *> leaders;
                                    for (auto item : avail_e.getOperands())
                                    {
                                        Operand *leader = avail_out[pred].find_leader(lookup(item));
                                        if (!leader)
                                        {
                                            logf("pred bb%d,find leader of val %s\n", pred->getNo(), item->toStr().c_str());
                                            logf("leader in bb%d:\n", pred->getNo());
                                            printset(avail_out[pred]);
                                            assert(leader);
                                        }
                                        leaders.push_back(leader);
                                    }
                                    Operand *t = gen_fresh_tmep(e);
                                    logf("gen fresh tmp %s\n", t->toStr().c_str());
                                    if (!lookup(avail_e))
                                        htable[avail_e] = t;
                                    ValueNr val = lookup(avail_e);
                                    htable[t] = val;
                                    logf("VALNR: %s\n", val->toStr().c_str());
                                    auto freash_inst = genInst(t, e.getOpcode(), leaders);
                                    // t <- inst
                                    for (auto inst = pred->rbegin(); inst != pred->rend(); inst = inst->getPrev())
                                    {
                                        if (inst->isCond() || inst->isUncond())
                                        {
                                            pred->insertBefore(freash_inst, inst);
                                            break;
                                        }
                                    }
                                    expr_cnt++;

                                    // avail_out[pred].insert(t);
                                    avail_out[pred].vrplc(t);
                                    // vinsert(avail_out[pred],genExpr(t));
                                    avail[pred] = t;
                                    // new_expr = true;
                                    // new_stuff = true;
                                    // I this the following statement is necessary, but the thesis didn't mention
                                    // new_sets[pred].insert(t);
                                    new_sets[pred].vinsert(t);
                                }
                            }
                            // only need to insert phi when actually generate new temp
                            // not mentioned as well in thesis
                            if (insert_phi)
                            {
                                phi_cnt++;
                                Operand *t = gen_fresh_tmep(e);
                                htable[t] = lookup(e);
                                logf("insert phi %s, val is %s\n", t->toStr().c_str(), lookup(e)->toStr().c_str());
                                avail_out[bb].vrplc(t);
                                // avail_out[bb].insert(t);

                                std::map<BasicBlock *, Operand *> args;
                                for (auto pair : avail)
                                {
                                    auto bb = pair.first;
                                    assert(pair.second.getOpcode() == ExprOp::TEMP);
                                    auto val = pair.second.getOperands()[0];
                                    args[bb] = val;
                                }
                                auto phi = genInst(t, ExprOp::PHI, args);
                                bb->insertFront(phi);
                                new_stuff = true;
                                // new_sets[bb].insert(t);
                                new_sets[bb].vrplc(t);
                            }
                        }
                    }
                }

                // pass new values to dom children
            }

            for (auto succ : domtree[bb])
            {
                q.push(succ);
                for (auto e : new_sets[bb])
                {
                    logf("bb%d pass %s to bb%d\n", bb->getNo(), e.tostr().c_str(), succ->getNo());
                    new_sets[succ].vinsert(e);
                    avail_out[succ].vrplc(e);
                }
            }
            new_sets[bb].clear();
        }
        // for(auto [bb,set] : new_sets){
        //     for(auto child : domtree[bb]){
        //         for(auto e : new_sets[bb]){
        //             logf("bb%d pass %s to bb%d\n",bb->getNo(),e.tostr().c_str(),child->getNo());
        //             new_sets[child].vinsert(e);
        //             avail_out[child].vrplc(e);
        //         }
        //     }
        //     new_sets[bb].clear();
        // }
    }
}

void GVNPRE_FUNC::elminate(Function *func)
{
    std::vector<Instruction *> torm;
    for (auto bb_it = func->begin(); bb_it != func->end(); bb_it++)
    {
        auto bb = *bb_it;

        // speed up find_leader:
        // for (auto e : avail_out[bb])
        // {
        //     avail_out[bb].leader_map[lookup(e)] = e.getOperands()[0];
        // }

        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            if (inst->hasNoDef())
                continue;
            if (inst->isBinary() || inst->isGep())
            {
                Operand *dst = inst->getDef();
                Operand *leader = avail_out[bb].find_leader(lookup(dst));
                // logf("value of %s is %s, leader is %s\n", dst->toStr().c_str(), lookup(dst)->toStr().c_str(), leader->toStr().c_str());
                if (leader != dst)
                {
                    inst->replaceAllUsesWith(leader);
                    torm.push_back(inst);
                }
            }
        }
    }
    erase_cnt = torm.size();
    for (auto i : torm)
    {
        // i->output();
        i->getParent()->remove(i);
        delete i;
        // decomment the delete statement when we are not in debug mode!
    }
}

void GVNPRE_FUNC::pass()
{
    addGval(func);
    rmcEdge(func);
    buildDomTree(func);
    buildSets(func);
    buildAntic(func);
    // for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    // {
    //     BasicBlock *bb = *it_bb;
    //     logf("bb%d:\n", bb->getNo());
    //     logf("avail_out:\n");
    //     printset(avail_out[bb]);
    //     logf("antic_in:\n");
    //     printset(antic_in[bb]);
    //     logf("\n");
    // }
    logf("perform insertion\n");

    insert(func);
    elminate(func);

    // fprintf(stderr, "[GVNPRE]:insert %d exprs,%d phis,erase %d exprs in func %s.\n", expr_cnt, phi_cnt, erase_cnt, func->getSymPtr()->toStr().c_str());
    if (expr_cnt != 0 && erase_cnt == 0)
        assert(0);
}

std::vector<Expr> Exprset::topological_sort()
{
    if (!changed)
        return topological_seq;
    topological_seq.clear();
    // the parameter set is expected to be canonical, i.e. no expr with same value number
    // if violated,,,??

    // compute value's topological order, then output the corresponding expr
    std::unordered_map<ValueNr, std::vector<ValueNr>> antigraph; // reversed the dependency graph
    std::unordered_map<ValueNr, unsigned> degree;
    std::unordered_map<ValueNr, Expr> val2e;

    for (const auto &e : exprs)
    {
        ValueNr val = lookup(e);
        assert(!val2e.count(val));
        val2e[val] = e;
        degree[val] = 0;
    }
    for (const auto &e : exprs)
    {
        if (e.getOpcode() == ExprOp::TEMP)
            continue;
        // iterate over the operands
        ValueNr val_e = lookup(e);
        for (auto operand : e.getOperands())
        {
            ValueNr val_operand = lookup(operand);
            if (!val2e.count(val_operand))
                continue;
            antigraph[val_operand].push_back(lookup(e));
            degree[val_e]++;
        }
    }
    // print anti-graph
    //  for(auto it = antigraph.begin();it!=antigraph.end();it++){
    //      logf("%s:",val2e[it->first].c_str());
    //      for(auto val : it->second)
    //          logf("%s,",val2e[val].c_str());
    //      logf("\n");
    //  }

    std::queue<ValueNr> q; // Expr with zero in degree after reverse the dependency graph
    for (auto it = degree.begin(); it != degree.end(); it++)
        if (it->second == 0)
            q.push(it->first);

    std::vector<ValueNr> toplist; // exprset in topology order
    while (!q.empty())
    {
        ValueNr val = q.front();
        q.pop();
        toplist.push_back(val);
        for (auto &to : antigraph[val])
        {
            degree[to]--;
            if (degree[to] == 0)
                q.push(to);
        }
    }

    std::vector<Expr> ret;
    for (auto val : toplist)
        ret.push_back(val2e[val]);
    return ret;

    changed = false;
    return topological_seq;
}

void GVNPRE::pass()
{
    for (auto func_it = unit->begin(); func_it != unit->end(); func_it++)
    {
        GVNPRE_FUNC pre(*func_it);
        pre.pass();
        htable.clear();
        trans_cache.clear();
        // htable must be clear here, because some expr can be used in multiple functions
        // causing some function use local value number of other functions
    }
    SimplifyCFG scfg(unit);

    scfg.pass();
}