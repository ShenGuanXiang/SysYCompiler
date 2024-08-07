# 线程库函数调用代码（在AST中调用）

```C++
auto ct_func = new FunctionType(TypeSystem::intType, std::vector<Type *>{nullptr});
new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{},
                        new IdentifierSymbolEntry(ct_func, "__create_threads", 0),
                        builder->getInsertBB());

auto jt_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType});
new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        // 如果参数为 0，执行系统调用 SYS_exit 来退出线程。不为 0 则会执行系统调用 SYS_waitid 来等待并回收线程资源。
                        std::vector<Operand *>{new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0))}, // 或者 1
                        new IdentifierSymbolEntry(jt_func, "__join_threads", 0),
                        builder->getInsertBB());

auto bc_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType, TypeSystem::intType});
new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 0)), new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, (1 << 4) - 1))},
                        new IdentifierSymbolEntry(jt_func, "__bind_core", 0),
                        builder->getInsertBB());


auto lc_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType});
auto _mutex = new IdentifierSymbolEntry(TypeSystem::intType, "_mutex_" + std::to_string(builder->getInsertBB()->getNo()), 0); // 或许name前面还需要加点啥，遇到了再说
SymbolEntry *addr_se_mu = new IdentifierSymbolEntry(*_mutex);
addr_se_mu->setType(new PointerType(_mutex->getType()));
auto addr_mu = new Operand(addr_se_mu);
_mutex->setAddr(addr_mu);
auto lock_inst = new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{new Operand(_mutex)},
                        new IdentifierSymbolEntry(lc_func, "__lock", 0),
                        builder->getInsertBB());
lock_inst->getParent()->getParent()->getParent()->insertDecl(_mutex);

new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{new Operand(_mutex)},
                        new IdentifierSymbolEntry(lc_func, "__unlock", 0),
                        builder->getInsertBB());


auto ba_func = new FunctionType(TypeSystem::voidType, std::vector<Type *>{TypeSystem::intType, TypeSystem::intType});
auto _barrier = new IdentifierSymbolEntry(TypeSystem::intType, "_barrier_" + std::to_string(builder->getInsertBB()->getNo()), 0); // 或许name前面还需要加点啥，遇到了再说
SymbolEntry *addr_se_ba = new IdentifierSymbolEntry(*_barrier);
addr_se_ba->setType(new PointerType(_barrier->getType()));
auto addr_ba = new Operand(addr_se_ba);
_barrier->setAddr(addr_ba);
auto ba_inst = new FuncCallInstruction(new Operand(new TemporarySymbolEntry(TypeSystem::intType, SymbolTable::getLabel())),
                        std::vector<Operand *>{new Operand(_barrier), new Operand(new ConstantSymbolEntry(TypeSystem::constIntType, 3 /*这个数字根据具体需要更改*/)) }, 
                        new IdentifierSymbolEntry(ba_func, "__barrier", 0),
                        builder->getInsertBB());
ba_inst->getParent()->getParent()->getParent()->insertDecl(_barrier);
```


# 已完成

## Inliner

非库函数&非递归函数全部内联

## 窥孔

1. ldr/str ..., [r] 和前面的 add r, imm/r 或 sub r, imm合并：

   - add r0, fp, #-12

     ldr r1, [r0]

     --->

     add r0, fp, #-12

     ldr r1, [fp, #-12]

   - add r0, fp, #-12

     str r1, [r0]

      --->

     add r0, fp, #-12
     
     str r1, [fp, #-12]
   
   - add vr7279, fp, #-12
   
     mov vr28908, #0
   
     str vr28908, [vr7279]
   
     --->
   
     add vr7279, fp, #-12
   
     mov vr28908, #0
   
     str vr28908, [fp, #-12]


2. ldr指令转为mov

   - str v355, [v11]

     ldr v227, [v11]

     --->

     str v355, [v11]

     mov v227,v355

   - ldr r0, [fp,#-12]

     ldr r1, [fp,#-12]

     --->

     ldr r0, [fp, #-12]

     mov r1, r0

3. 前后衔接的(v)mul和(v)add/(v)sub指令合并为(v)mla/(v)mls

   - mul v0,v1,v2

     add v3,v4,v0

     --->

     mul v0,v1,v2

     mla v3,v1,v2,v4

   - vmul.f32 v0,v1,v2

     vadd.f32 v3,v4,v0

     --->

     vmul.f32 v0,v1,v2

     vmla.f32 v4,v1,v2

     vmov.f32 v3,v4


4. mov vr1/vs1, vr2/vs2，前面一条指令是binary/mov等latency等于mov的指令且只有单个目标且为vr2/vs2(或mov的目标为r0-r3/s0-s3)：

   - add r5, r0, 1 

     mov r0, r5

     ---->

     add r5, r0, 1（很可能r5不会再用到，可以删掉） 

     add r0, r0, 1

   - mov r1, r0

     mov r0, r1

     ---->

     mov r1, r0

     mov r0, r0

5. mov移位 + add/sub/rsb/mov  

   - mov v5, v2, lsl #2

     add v4, v3, v5 (add v4, v5, v3)

     --->

     add v4, v3, v2, lsl #2

     mov v5, v2, lsl #2

     (寄存器不能重定义，v4!=v2 且 v4!=v5)

6. ldr/str 移位

    - add r4, r2, r1, LSL #2

      mov r3, #0

      str r3, [r4] 

      --->

      add r4, r2, r1, LSL #2

      mov r3, #0

      str r3, [r2, r1, LSL #2] (浮点不行) （放在add r4, r2, r1, LSL #2的优化后面）

    - add r7, r5, r6, LSL #2
    
      ldr r5, [r7]
    
      --->
    
      add r7, r5, r6, LSL #2
    
      ldr r5, [ r5, r6, LSL #2] （浮点不行）（放在add r7, r5, r6, LSL #2的优化后面）
      
    - lsl vr371, vr27, #2
    
      ldr vr189, [vr181, vr371]
    
      ->
    
      lsl vr371, vr27, #2
    
      ldr vr189, [vr181, vr27, lsl #2]
    
    - lsl r5, r3, #2
    
      str r4, [r6, r5]     
    
      ->
      
      lsl r5, r3, #2
      
      str r4, [r6, r3, lsl #2]     

    - lsl r3, r2, #2

      add r12, r5, r2, lsl #2

      ldr r3, [r5, r3] 
      
      ->
## memset

## 寄存器分配

- 删除def not use的指令

- 生存期合并（coalesce）

- r0-r3 fp lr等也参与分配

## Global2Local

- 对于全局从未发生store的int/float类型变量，将其视为常数处理。
  
- 部分全局int/float类型变量转换为main函数内的局部变量。
  
- 优化main函数开头的全局int/float变量的读写
## Mem2Reg

- 除全局变量、数组外都转到寄存器

## 常量优化

- 除数组以外的const变量都直接内联

- SCCP
  
- 常量数组+常量索引的情况在SCCP中被消掉
  
- if(a == 1) 分支内部a用1替换

## 循环倒置

- while转为if do while

## 死代码消除

- ir dce
    - iscritical: 判断Instruciton、Function是否涉及到输入输出或者对内存数据的修改
    - getRDF: 找到反向支配边界，换言之为正向的被支配边界
    - get_nearest_dom: 反向找到控制流最近的岔路交点
- asm dce
    - iscritical: 判断汇编指令是否可以被删，包括函数调用后对栈帧的调整，如果是条件跳转，那么需要判断他的跳转对象是否也是跳转指令，如果是就不删了
    - 删除掉了只包含一条跳转指令的machineblock(合并，类似于控制流优化), 直接调用SingleBrDelete(MachineFunc*)

## 强度削弱

- 整数乘除2的幂次转移位

## 公共子表达式删除

- 中端&后端

## 中端控制流优化

- 删除不可达的基本块。
- 如果仅有一个前驱且该前驱仅有一个后继，将基本块与前驱合并。
- 消除空的基本块和仅包含无条件分支的基本块。
- 简化无条件跳转到只有一个ret语句的基本块的情况。

# TODO

## 图着色寄存器分配

添加浮点、更改spillcost

## 寄存器溢出到局部数组下方、保存常量的寄存器优先溢出

## Inliner

- 用散列表保存单参数的递归函数的返回值(记忆化搜索)
- 递归函数展开若干层
- 尾递归转循环

## 代数化简

- 代数恒等式化简 +--a !!a a\*b+a\*c a\*b/b 数组寻址表达式 a+0 a\*1 a/1 a*0 a%1
- (i-1)*1024 + j-1
- a+a = a*2, a-a = 0 a/a = 1 a%a = 0
- a+b+b+b+...

## 循环优化

- 不变代码外提 & 部分冗余消除

- 循环中的强度削弱
  https://en.wikipedia.org/wiki/Induction_variable
  
  https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/ive/

## 指令调度

- 乱序执行
- 调整基本块内指令顺序，尽可能缩短变量的生存期，从而减少寄存器分配中的溢出

## Global Code Motion

## 访存优化

- 对不写内存的函数调用，分析读的内存是否发生改变，消除多余的调用
- 如果局部变量的每个下标都只被赋值过一次，且赋值为常数，则将其提升为全局常量
- 拷贝传播：尽可能将读内存操作替换为上次读或写的值，对于数组没有被修改过的情况，将对数组的固定下标的访问替换为数组全局初始化的值
- 如果写操作不会对之后的读操作产生影响，则删除写操作

## 后端控制流优化

- 对于只有一个前驱的基本块，将指令移动到前驱
- 将无条件跳转的目标基本块的代码复制到跳转前的基本块
- 多个空的machineblock合并成1个

## 强度削弱

- 乘以非2的幂次
- 除以非2的幂次
- 除以2的幂次，立即数超范围的情况，分解
- 取模?

## SIMD

- 循环展开
  - 对于循环次数固定且展开后指令数较小的循环，展开为顺序执行
  - 对于循环次数不固定的循环，进行循环展开
- SIMD:如果一个循环中没有分支，且每次循环间无数据依赖，且每条指令都能换成SIMD指令，且SIMD寄存器够用，则替换为SIMD指令

## 寄存器分配

- spillweight：生命期、循环/IF ELSE深度、use_density、冲突的liveinterval个数

- liveintervalsplit 

- rewriter重写溢出代码

## 多线程

- 循环合并
- 多线程：对于一个k次迭代的循环，如果判断循环间无数据依赖，则将其分为num_threads块并行执行

## 死代码消除

  后端增加生存期合并

## Global2Local

- 在函数内发生函数调用前，store被调函数需要load的全局变量，调用后load被调函数store的全局变量。
- 返回前store所有发生修改的全局变量
- 全局数组局部化

## 常量优化

- 一部分副作用优化

- 范围估计

  %t26 = phi i32 [ 0 , %B5 ], [ 51 , %B12 ]

  %t27 = phi i32 [ 0 , %B5 ], [ %t30 , %B12 ]

  %t0 = icmp slt i32 %t26, 100

## 后端指令从vector改为链表形式，加速删除、插入

## 窥孔优化

1. 在op1之前：

   ldr r3, =4094

   add/sub r0, fp, r3

   ldr r1, [r0]

   --->

   ldr r3, =4094

   add/sub r0, fp, r3

   ldr r1, [fp, #4094/#-4094]

2. // TODO：next_inst为vmov/mov && next_inst目标为r0~r3/s0~s3 && curr_inst目标 = next_inst源, curr_inst可以是其它指令=>

3. 将多条store替换为vdup和vstm【暂时不做了 :) 】

4. 多条push/pop合并为一条push/pop【发现有个bug :( 】

## 参考网站

https://oi-wiki.org/lang/optimizations/

https://gitlab.eduxiji.net/nscscc/compiler2023/ 性能样例

