// #include "Multithread.h"
// #include <algorithm>

// static inline Operand* new_const_op(int val){
//     return new Operand(new ConstantSymbolEntry(TypeSystem::constIntType,val));
// }
// static inline Operand* copy_op(Operand* op){
//     auto sym_entry = op->getEntry();
//     if(sym_entry->isConstant() || sym_entry->isVariable())
//         return op;
//     return new Operand(new TemporarySymbolEntry(sym_entry->getType(),SymbolTable::getLabel()));
// }
// static  BasicBlock* insert_bb_on_edge(BasicBlock* from, BasicBlock* to){
//     auto func = from->getParent();
//     assert(func == to->getParent());
//     auto newbb = new BasicBlock(func);
//     new UncondBrInstruction(to, newbb);
//     from->removeSucc(to);
//     to->removePred(from);
//     from->addSucc(newbb);
//     newbb->addPred(from);
//     to->addPred(newbb);
//     newbb->addSucc(to);
//     // modify branch in from
//     for (auto inst = from->rbegin(); inst != from->rend(); inst = inst->getPrev())
//     {
//         if (inst->isCond())
//         {
//             auto br = dynamic_cast<CondBrInstruction *>(inst);
//             if (br->getTrueBranch() == to)
//             {
//                 br->setTrueBranch(newbb);
//             }
//             if (br->getFalseBranch() == to)
//             {
//                 br->setFalseBranch(newbb);
//             }
//         }
//         else if (inst->isUncond())
//         {
//             auto br = dynamic_cast<UncondBrInstruction *>(inst);
//             if (br->getBranch() == to)
//             {
//                 br->setBranch(newbb);
//             }
//         }
//         else
//             break;
//     }
//     // modify phi in to
//     for (auto inst = to->begin(); inst != to->end(); inst = inst->getNext())
//     {
//         if (inst->isPHI())
//         {
//             auto phi = dynamic_cast<PhiInstruction *>(inst);
//             auto &src = phi->getSrcs();
//             if (src.count(from))
//             {
//                 src[newbb] = src[from];
//                 src.erase(from);
//             }
//         }
//         else
//             break;
//     }
//     return newbb;
// }
// static void copy_bb(BasicBlock*from, BasicBlock*to){
//     std::unordered_map<Operand*,Operand*> old2copy;
//     // a mapping from old operand (defined in from) to new operand
//     for(auto inst = from->begin();inst!=from->end();inst=inst->getNext()){
//         Instruction* inst_copy = inst->copy();
//         for(auto& u:inst_copy->getUses())
//             if(old2copy.count(u))
//                 u = old2copy[u];
//         inst_copy->setParent(to);
//         if(!inst_copy->hasNoDef()){
//             Operand* old_def_op = inst_copy->getDef();
//             old2copy[old_def_op] = copy_op(old_def_op);
//             inst_copy->setDef(old2copy[old_def_op]);
//             // !!!!!!
//             // TODO : some variable may be defined in the loop, and used outside the loop
//             // copying the definition should insert phi outside the loop
//         }
//         to->insertBack(inst_copy);
//     }
// }
// void Multithread::insert_opt_jump()
// {
//     const std::vector<BasicBlock*> preds;
//     std::copy(loop.loop_header->pred_begin(),loop.loop_header->pred_end(),std::back_inserter(preds));
//     BasicBlock* judge_bb = new BasicBlock(loop.loop_header->getParent());
    
// }
// void Multithread::transform()
// {
//     // 1. calculate iterate times
//     Operand* loop_span_op;
//     // 2. calculate the range of each thread
//     // 3. copy loop code for each thread
//     // 4. add multi thread branch
// }