#include "gvnpre.h"
#include "SimplifyCFG.h"
#include <queue>

void logf(const char *formmat, ...)
{
#ifdef DEBUG_GVNPRE

    va_list args;
    va_start(args, formmat);
    vfprintf(stderr, formmat, args);
    va_end(args);
#endif
}
void printset(Exprset set)
{
#ifdef DEBUG_GVNPRE

    if (set.empty())
        logf("empty\n");
    else
    {
        for (auto pair : set)
        {
            std::string valstr = pair.first->toStr();
            std::string exprstr = pair.second.tostr();
            logf("%s:%s\n", valstr.c_str(), exprstr.c_str());
        }
        logf("\n");
    }
#endif
}

std::vector<Expr> topological(Exprset set)
{
    // convert set to array in topological order
    std::unordered_map<Expr, std::vector<Expr>> antigraph; // reversed the dependency graph
    std::unordered_map<Expr, unsigned> degree;
    std::queue<Expr> q;        // Expr with zero in degree after reverse the dependency graph
    std::vector<Expr> toplist; // exprset in topology order
    Exprset newset;
    for (auto pair : set)
    {
        Expr e = pair.second;
        std::string str = pair.first->toStr();
        degree[e] = 0;
    }
    for (auto it = set.begin(); it != set.end(); it++)
    {
        Expr e = it->second;
        if (e.op == 0)
            continue;
        for (auto val : e.vals)
        {
            if (!set.count(val))
                continue;
            Expr op = set[val];
            antigraph[op].push_back(e);
            degree[e]++;
        }
    }
    for (auto it = degree.begin(); it != degree.end(); it++)
    {
        if (it->second == 0)
            q.push(it->first);
    }
    while (!q.empty())
    {
        Expr e = q.front();
        q.pop();
        toplist.push_back(e);
        for (auto &to : antigraph[e])
        {
            degree[to]--;
            if (degree[to] == 0)
                q.push(to);
        }
    }
    return toplist;
}

Expr GVNPRE::genExpr(Instruction *inst)
{
    Expr expr;
    switch (inst->getInstType())
    {
    case Instruction::BINARY:
    {
        auto binst = dynamic_cast<BinaryInstruction *>(inst);
        switch (binst->getOpcode())
        {
        case BinaryInstruction::ADD:
            expr.op = Expr::ADD;
            break;
        case BinaryInstruction::SUB:
            expr.op = Expr::SUB;
            break;
        case BinaryInstruction::MUL:
            expr.op = Expr::MUL;
            break;
        case BinaryInstruction::DIV:
            expr.op = Expr::DIV;
            break;
        case BinaryInstruction::MOD:
            expr.op = Expr::MOD;
            break;
        default:
            assert(0);
        }
        for (auto use : binst->getUses())
        {
            Expr use_expr = genExpr(use);
            Operand *use_val = lookup(use_expr);
            expr.vals.push_back(use_val);
        }
        break;
    }
    default:
        assert(0);
    }
    return expr;
}

Expr GVNPRE::genExpr(Operand *temp)
{
    Expr newexpr;
    newexpr.vals.push_back(temp);
    auto t = temp->getType();
    if (t->isConst())
    {
        if (t->isConstInt())
        {
            int v = temp->getEntry()->getValue();
            if (!int2Expr.count(v))
            {
                int2Expr[v] = newexpr;
                htable[newexpr] = temp;
            }
            return int2Expr[v];
        }
        else if (t->isConstFloat())
        {
            float v = temp->getEntry()->getValue();
            if (!float2Expr.count(v))
            {
                float2Expr[v] = newexpr;
                htable[newexpr] = temp;
            }
            return float2Expr[v];
        }
        else
            return newexpr;
    }
    else
        return newexpr;
}

Instruction *GVNPRE::genInst(Operand *dst, unsigned op, std::vector<Operand *> leaders)
{
    Instruction *inst;
    switch (op)
    {
    case Expr::ADD:
        inst = new BinaryInstruction(BinaryInstruction::ADD, dst, leaders[0], leaders[1]);
        break;
    case Expr::SUB:
        inst = new BinaryInstruction(BinaryInstruction::SUB, dst, leaders[0], leaders[1]);
        break;
    case Expr::MUL:
        inst = new BinaryInstruction(BinaryInstruction::MUL, dst, leaders[0], leaders[1]);
        break;
    case Expr::DIV:
        inst = new BinaryInstruction(BinaryInstruction::DIV, dst, leaders[0], leaders[1]);
        break;
    case Expr::MOD:
        inst = new BinaryInstruction(BinaryInstruction::MOD, dst, leaders[0], leaders[1]);
        break;
    default:
        assert(0);
    }
    return inst;
}

Instruction *GVNPRE::genInst(Operand *dst, unsigned op, std::map<BasicBlock *, Operand *> phiargs)
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

Operand *GVNPRE::lookup(Expr expr)
{
    if (expr.op != 0)
        return htable.count(expr) ? htable[expr] : nullptr;

    Operand *temp = expr.vals[0];
    Type *t = temp->getType();
    if (temp->getEntry()->isConstant())
    {
        if (t->isConstInt())
        {
            int v = temp->getEntry()->getValue();
            if (!int2Expr.count(v))
                int2Expr[v] = expr;
            return int2Expr[v].vals[0];
        }
        else if (t->isConstFloat())
        {
            float v = temp->getEntry()->getValue();
            if (!float2Expr.count(v))
                float2Expr[v] = expr;
            return float2Expr[v].vals[0];
        }
        else
            assert(0);
    }
    else
        return htable.count(expr) ? htable[expr] : nullptr;
}

void GVNPRE::vinsert(Exprset &set, Expr expr)
{
    if (!htable.count(expr))
    {
        std::string name = expr.vals[0]->toStr();
        assert(htable.count(expr));
    }
    Operand *val = htable[expr];
    if (!set.count(val))
        set[val] = expr;
}

Exprset GVNPRE::phi_trans(Exprset set, BasicBlock *from, BasicBlock *to)
{
    // convert set to array in topological order
    std::vector<Expr> toplist = topological(set);
    Exprset newset;
    // translate each expression in order
    std::unordered_map<Operand *, Operand *> trans_cache;
    for (size_t i = 0; i < toplist.size(); i++)
    {
        Expr e = toplist[i];
        if (e.op == 0) // tempraray
        {
            Instruction *definst = e.vals[0]->getDef();
            if (definst && definst->getInstType() == Instruction::PHI && definst->getParent() == to)
            {
                ValueNr oldval = htable[e];
                Operand *newtemp = dynamic_cast<PhiInstruction *>(definst)->getSrcs()[from];
                e.vals[0] = newtemp;

                ValueNr newval = lookup(e);
                trans_cache[oldval] = newval;
                newset[newval] = e;
            }
            else
                newset[lookup(e)] = e;
        }
        else
        { // expression
            bool changed = false;
            ValueNr oldval = lookup(e);
            for (auto &val : e.vals)
            {
                if (trans_cache.count(val))
                {
                    val = trans_cache[val];
                    changed = true;
                }
            }
            if (changed)
            {
                // e's composite val is changed now, e is not ever the old e
                if (!htable.count(e)) // modify htable in the process (insert new mapping)
                    htable[e] = new Operand(new TemporarySymbolEntry(oldval->getType(), SymbolTable::getLabel()));
                ValueNr newval = lookup(e);
                trans_cache[oldval] = newval;
            }
            newset[lookup(e)] = e;
        }
    }

    return newset;
}

Expr GVNPRE::phi_trans(Expr expr, BasicBlock *from, BasicBlock *to)
{
    // expr is bound to defined in bb'to'
    std::vector<Expr> toplist = topological(antic_in[to]);
    Exprset newset;
    // translate each expression in order
    std::unordered_map<Operand *, Operand *> trans_cache;
    for (size_t i = 0; i < toplist.size(); i++)
    {
        bool flag = false;
        Expr e = toplist[i];
        if (e == expr)
            flag = true;
        if (e.op == 0) // tempraray
        {
            Instruction *definst = e.vals[0]->getDef();
            if (definst && definst->getInstType() == Instruction::PHI && definst->getParent() == to)
            {
                ValueNr oldval = htable[e];
                Operand *newtemp = dynamic_cast<PhiInstruction *>(definst)->getSrcs()[from];
                e.vals[0] = newtemp;

                ValueNr newval = lookup(e);
                trans_cache[oldval] = newval;
                newset[newval] = e;
            }
            else
                newset[lookup(e)] = e;
        }
        else
        { // expression
            bool changed = false;
            ValueNr oldval = lookup(e);
            for (auto &val : e.vals)
            {
                if (trans_cache.count(val))
                {
                    val = trans_cache[val];
                    changed = true;
                }
            }
            if (changed)
            {
                // e's composite val is changed now, e is not ever the old e
                if (!htable.count(e)) // modify htable in the process (insert new mapping)
                    htable[e] = new Operand(new TemporarySymbolEntry(oldval->getType(), SymbolTable::getLabel()));
                ValueNr newval = lookup(e);
                trans_cache[oldval] = newval;
            }
            newset[lookup(e)] = e;
        }
        if (flag)
            return e;
    }

    assert(0);

    // //translate single expression via one phi seems need no cache?
    // //not necessary, but cache can tranlate more expression, recognize more exprs
    // if(expr.op!=0){//v1 op v2 etc.
    //     for(auto& val : expr.vals){
    //         if(val->defsNum() &&val->getDef()->isPHI() && val->getDef()->getParent()==to){
    //             auto phi = dynamic_cast<PhiInstruction*>(val->getDef());
    //             Operand* newtemp = phi->getSrcs()[from];
    //             ValueNr newval = lookup(newtemp);
    //             val = newval;
    //         }
    //     }
    // }
    // //this function is used in insert phase
    // //no need to add new value, cuz the new value will be tested later
    // return expr;
}

void GVNPRE::clean(Exprset &set)
{
    // it seems that we don't need maintain 'kill' set ?
    // logf("before cleaning:\n");
    // printset(set);
    std::vector<Expr> toplist = topological(set);
    for (const Expr &e : toplist)
    {
        ValueNr v = lookup(e);
        assert(v != nullptr);
        if (e.op == 0)
            continue;
        for (auto val : e.vals)
        {
            if (!set.count(val))
            {
                set.erase(v);
                break;
            }
        }
    }
    // logf("after cleaning:\n");
    // printset(set);
    // TODO : could the dag be separated into several parts?
}

// void GVNPRE::clean(Exprset &set)
// {
//     logf("before cleaning:\n");
//     printset(set);

//     std::vector<ValueNr> torm;
//     for(auto pair : set){
//         ValueNr val = pair.first;
//         Expr e = pair.second;
//         if(e.op==0) continue;
//         for(auto v : e.vals){
//             if(!set.count(v)){
//                 torm.push_back(val);
//                 continue;
//             }
//         }
//     }
//     for(auto v : torm)
//         set.erase(v);

//     //convert set to array in topological order
//     std::unordered_map<Expr*,std::vector<Expr*>> antigraph;//reversed the dependency graph
//     std::unordered_map<Expr*,unsigned> degree;
//     std::queue<Expr*>q; //Expr with zero in degree after reverse the dependency graph
//     std::vector<Expr*>toplist; //exprset in topology order
//     for(auto& pair : set){
//         Expr* e = &pair.second;
//         degree[e]=0;
//     }

//     for(auto it=set.begin();it!=set.end();it++){
//         Expr* e = &it->second;
//         if(e->op==0) continue; // tempraray points to itself, no need to reverse this dependency
//         for(auto val : e->vals){
//             if(!set.count(val)) {
//                 logf("expr's val not in set:%s\n",e->tostr().c_str());
//                 logf("not in set:%s\n",val->toStr().c_str());
//                 logf("set:\n");
//                 printset(set);
//                 assert(set.count(val));
//             }
//             Expr* op = &set[val];
//             antigraph[op].push_back(e);
//             degree[e]++;
//         }
//     }
//     for(auto it=degree.begin();it!=degree.end();it++){
//         if(it->second==0)
//             q.push(it->first);
//     }
//     while(!q.empty()){
//         Expr* e =q.front();
//         q.pop();
//         toplist.push_back(e);
//         for(auto op : antigraph[e]){
//             degree[op]--;
//             if(degree[op]==0) q.push(op);
//         }
//     }

//     //clean
//     for(size_t i=0;i<toplist.size();i++){
//         Expr* e = toplist[i];
//         bool tokill=false;
//         for(auto val : e->vals){
//             if(kill.count(val)){
//                 tokill=true;
//                 break;
//             }
//         }
//         if(tokill){
//             auto val = htable[*e];
//             set.erase(val);
//             kill.insert(val);
//         }
//     }
//     // logf("after cleaning:\n");
//     // printset(set);
// }

Operand *GVNPRE::find_leader(Exprset set, ValueNr val)
{
    // when phi_tran gen new value, the new value has no leader
    if (!val)
        return nullptr;
    if (val->getEntry()->isConstant())
        return val; // all literal constant has its leader anywhere
    if (val->getEntry()->isVariable())
        return val; // function param, global also hold
    if (!set.count(val))
        return nullptr;
    else
        return set[val].vals[0];
    // assert(set[val].op==0)
    // if(!htable.count(val)) return nullptr;
    // if(!htable.count(val)) return nullptr;
}

Operand *GVNPRE::gen_fresh_tmep(Expr e)
{
    // gen new temp as e's type
    Operand *val = lookup(e);
    assert(val);
    return new Operand(new TemporarySymbolEntry(val->getEntry()->getType(), SymbolTable::getLabel()));
}

void GVNPRE::rmcEdge(Function *func)
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

void GVNPRE::buildDomTree(Function *func)
{
    func->ComputeDom();
    for (auto it_bb = func->begin(); it_bb != func->end(); it_bb++)
    {
        BasicBlock *bb = *it_bb;
        if (!bb->getIDom())
            continue;
        domtree[bb->getIDom()].push_back(bb);
    }
}

void GVNPRE::gvnpre(Function *func)
{
    for (auto param : func->getParamsOp())
    {
        Expr newexpr;
        newexpr.vals.push_back(param);
        htable[newexpr] = param;
    }
    rmcEdge(func);
    buildDomTree(func);
    buildSets(func);
    buildAntic(func);
    insert(func);
    buildSets(func);
    elminate(func);
}

void GVNPRE::buildSets(Function *func)
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

            Expr dst = genExpr(inst->getDef());
            if (inst->isPHI())
            {
                Operand *vnr = inst->getDef();
                htable[dst] = vnr;
                phi_gen[bb][vnr] = dst;
            }
            else if (inst->isBinary())
            {
                auto expr = genExpr(inst);
                if (!htable.count(expr))
                    htable[expr] = inst->getDef();
                htable[dst] = htable[expr];
                for (auto use : inst->getUses())
                {
                    auto e = genExpr(use);
                    vinsert(expr_gen[bb], e);
                }
                vinsert(expr_gen[bb], expr);
                tmp_gen[bb][inst->getDef()] = dst;
            }
            else if (!inst->hasNoDef())
            {
                htable[dst] = inst->getDef();
                tmp_gen[bb][inst->getDef()] = dst;

                kill.insert(inst->getDef());

                // only delete for vundrun testcase!!!!!!!
                // decomment later!!!
            }
            vinsert(avail_out[bb], dst);
        }

        // for(auto succ : domtree[bb])
        //     q.push(succ);
        if (domtree.count(bb))
            for (auto succ_it = domtree[bb].rbegin(); succ_it != domtree[bb].rend(); succ_it++)
                q.push(*succ_it);
    }
}

void GVNPRE::buildAntic(Function *func)
{
    bool changed = true;
    while (changed)
    {
        changed = false;

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
                    std::vector<ValueNr> toerase;
                    for (auto it = _antic_out.begin(); it != _antic_out.end(); it++)
                    {
                        ValueNr val = it->first;
                        if (!find_leader(antic_in[succ], val))
                        {
                            toerase.push_back(val);
                        }
                    }
                    for (auto val : toerase)
                    {
                        _antic_out.erase(val);
                    }
                }
            }
            Exprset S = _antic_out;
            for (auto it = tmp_gen[bb].begin(); it != tmp_gen[bb].end(); it++)
            {
                auto val = it->first;
                auto e = it->second;
                if (S.count(val) && S[val] == e)
                    S.erase(val);
            }
            antic_in[bb] = expr_gen[bb];
            for (auto it = tmp_gen[bb].begin(); it != tmp_gen[bb].end(); it++)
            {
                auto val = it->first;
                auto e = it->second;
                if (antic_in[bb].count(val) && antic_in[bb][val] == e)
                    antic_in[bb].erase(val);
            }
            for (auto it = S.begin(); it != S.end(); it++)
            {
                auto val = it->first;
                if (!antic_in[bb].count(val))
                    antic_in[bb][val] = it->second;
            }
            clean(antic_in[bb]);

            if (old != antic_in[bb])
                changed = true;

            // for(auto succ : domtree[bb])
            //     q.push(succ);
            if (domtree.count(bb))
                for (auto succ_it = domtree[bb].rbegin(); succ_it != domtree[bb].rend(); succ_it++)
                    q.push(*succ_it);
        }
    }
}

void GVNPRE::insert(Function *func)
{
    bool new_stuff = true;
    while (new_stuff)
    {
        new_stuff = false;

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
            // if(dom){
            //     new_sets[bb]=new_sets[dom];
            //     for(auto pair: new_sets[dom])
            //         avail_out[bb][pair.first]=pair.second;
            // }

            if (bb->getNumOfPred() > 1)
            {
                std::vector<Expr> toplist = topological(antic_in[bb]);
                logf("toplist: of bb%d:\n", bb->getNo());
                // print toplist
                for (auto expr : toplist)
                    logf("%s ", expr.tostr().c_str());
                logf("\n");

                for (auto e : toplist)
                {
                    Operand *val = lookup(e);
                    if (e.op != 0)
                    { // v1 op v2 etc.
                        if (find_leader(avail_out[dom], val))
                            continue;
                        std::unordered_map<BasicBlock *, Expr> avail;
                        bool by_some = false, all_same = true;
                        Expr first_s;
                        for (auto pred_it = bb->pred_begin(); pred_it != bb->pred_end(); pred_it++)
                        {
                            auto pred = *pred_it;
                            // TODO: optimize 'phi_trans' to avoid calulating the whole set
                            Expr trans_e = phi_trans(e, pred, bb);
                            ValueNr trans_val = lookup(trans_e);
                            if (Operand *temp_leader = find_leader(avail_out[pred], trans_val))
                            {
                                Expr avail_e = genExpr(temp_leader);
                                avail[pred] = avail_e;
                                by_some = true;
                                if (first_s.vals.empty())
                                    first_s = avail_e;
                                else if (lookup(first_s) != lookup(avail_e))
                                    all_same = false;
                            }
                            else
                            {
                                avail[pred] = trans_e;
                                all_same = false;
                            }
                        }
                        if (!all_same && by_some)
                        {
                            bool new_expr = false;
                            for (auto pred_it = bb->pred_begin(); pred_it != bb->pred_end(); pred_it++)
                            {
                                auto pred = *pred_it;
                                Expr avail_e = avail[pred];
                                if (avail_e.op != 0)
                                {
                                    logf("copy %s from bb%d to bb%d\n", avail_e.tostr().c_str(), bb->getNo(), pred->getNo());
                                    new_stuff = true;
                                    std::vector<Operand *> leaders;
                                    for (auto val : avail_e.vals)
                                    {
                                        Operand *leader = find_leader(avail_out[pred], val);
                                        if (!leader)
                                        {
                                            logf("pred bb%d,find leader of val %s\n", pred->getNo(), val->toStr().c_str());
                                            logf("leader in bb%d:\n", pred->getNo());
                                            printset(avail_out[pred]);
                                            assert(leader);
                                        }
                                        leaders.push_back(leader);
                                    }
                                    Operand *t = gen_fresh_tmep(e);
                                    logf("gen fresh tmp %s\n", t->toStr().c_str());
                                    auto freash_inst = genInst(t, e.op, leaders);
                                    // t <- inst
                                    for (auto inst = pred->rbegin(); inst != pred->rend(); inst = inst->getPrev())
                                    {
                                        if (inst->isCond() || inst->isUncond())
                                        {
                                            pred->insertBefore(freash_inst, inst);
                                            break;
                                        }
                                    }
                                    if (!lookup(avail_e))
                                        htable[avail_e] = t;
                                    ValueNr val = lookup(avail_e);
                                    logf("VALNR: %s\n", val->toStr().c_str());
                                    htable[genExpr(t)] = val;
                                    avail_out[pred][val] = genExpr(t);
                                    // vinsert(avail_out[pred],genExpr(t));
                                    avail[pred] = genExpr(t);
                                    new_expr = true;
                                    new_stuff = true;
                                }
                            }
                            if (new_expr)
                            {
                                Operand *t = gen_fresh_tmep(e);
                                htable[genExpr(t)] = lookup(e);
                                avail_out[bb][lookup(e)] = genExpr(t);
                                // 这个phi生成的不知对不对，，
                                std::map<BasicBlock *, Operand *> args;
                                for (auto pair : avail)
                                {
                                    auto bb = pair.first;
                                    auto val = pair.second.vals[0];
                                    args[bb] = val;
                                }
                                auto phi = genInst(t, Expr::PHI, args);
                                bb->insertFront(phi);
                                // t <- phi
                                //  avail_out[bb][t]=genExpr(t);
                                //  new_sets[bb][lookup(e)]=genExpr(t);
                                vinsert(avail_out[bb], genExpr(t));

                                // vinsert(new_sets[bb],t);
                                new_stuff = true;
                            }
                        }
                    }
                }
            }

            for (auto succ : domtree[bb])
                q.push(succ);
        }
        for (auto bb_it = func->begin(); bb_it != func->end(); bb_it++)
        {
            avail_out[*bb_it].clear();
            expr_gen[*bb_it].clear();
            tmp_gen[*bb_it].clear();
            phi_gen[*bb_it].clear();
        }
        buildSets(func);
    }
}

void GVNPRE::elminate(Function *func)
{
    for (auto bb_it = func->begin(); bb_it != func->end(); bb_it++)
    {
        auto bb = *bb_it;

        std::vector<Instruction *> torm;
        for (auto inst = bb->begin(); inst != bb->end(); inst = inst->getNext())
        {
            if (inst->hasNoDef())
                continue;
            Operand *dst = inst->getDef();
            Operand *leader = find_leader(avail_out[bb], lookup(genExpr(dst)));
            if (leader != dst)
            {
                inst->replaceAllUsesWith(leader);
                torm.push_back(inst);
            }
        }
        for (auto i : torm)
        {
            bb->remove(i);
            delete i;
        }
    }
}

void GVNPRE::pass()
{
    for (auto func_it = unit->begin(); func_it != unit->end(); func_it++)
        gvnpre(*func_it);
    SimplifyCFG scfg(unit);
    scfg.pass();
}
