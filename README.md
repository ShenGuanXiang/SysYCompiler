# 已完成

## memset

## 寄存器分配

- 删除def not use的指令

- 生存期合并（coalesce）

- r0-r3 fp lr等也参与分配

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

## Inliner

- 非库函数&非递归函数全部内联，递归函数展开若干层

- 大体实现方式：
	- 深拷贝函数内所有指令
	
	- 函数体中所有对参数的使用替换为FuncCall 的 param
	
	- 如果有多个exit则ret替换为无条件跳转，只有一个exit则直接删除ret
	
	- ret结果替换原来funcCall定义的所有使用
	
- 如果一个函数的某个参数在所有调用中相同，则将其替换为常量
- 如果一个函数的返回值从未由调用者使用，则将返回值设为0
- 尾递归转循环
- 用散列表保存单参数的递归函数的返回值

## 窥孔优化

1、2、3在shm ppt上

定义一个窗口大小常量

1. ldr/str ..., [r] 和前面的 add r, imm/r 或 sub r, imm合并：

   - add r0, fp, #-12
     
     ldr r1, [r0]
     
     --->
     
     add r0, fp, #-12
     
     ldr r1, [fp, #-12]
   
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
   
6. 将多条store替换为vdup和vstm

7. 多条push/pop合并为一条push/pop（比如函数调用传参时）



## 寄存器分配

- spillweight：生命期、循环/IF ELSE深度、use_density、冲突的liveinterval个数

- liveintervalsplit 

- rewriter重写溢出代码

## 后端控制流优化

- 对于只有一个前驱的基本块，将指令移动到前驱
- 将无条件跳转的目标基本块的代码复制到跳转前的基本块
- 多个空的machineblock合并成1个

## Global2Local

- 做的不一样，等内联完再看一下

- 对于全局从未发生store的全局变量，将其视为常数处理。
- 对于全局int/float类型变量，转换为函数内的局部变量。
- 将main函数开头的全局变量写操作吸收到全局变量初始化中
- 在函数内发生函数调用前，store被调函数需要load的全局变量，调用后load被调函数store的全局变量。
- 返回前store所有发生修改的全局变量

## 常量优化

- 一部分副作用优化
- 范围估计
  %t26 = phi i32 [ 0 , %B5 ], [ 51 , %B12 ]
  %t27 = phi i32 [ 0 , %B5 ], [ %t30 , %B12 ]
  %t0 = icmp slt i32 %t26, 100

## 循环优化

- 不变代码外提
- 循环合并
- 循环中的强度削弱
  https://en.wikipedia.org/wiki/Induction_variable
- 循环展开
  - 对于循环次数固定且展开后指令数较小的循环，展开为顺序执行
  - 对于循环次数不固定的循环，进行循环展开

## 代数化简

- 代数恒等式化简 +--a !!a a\*b+a\*c a\*b/b 数组寻址表达式 a+0 a\*1 a/1 a*0 a%1

## 强度削弱

- 乘以非2的幂次
- 除以非2的幂次
- 除以2的幂次，立即数超范围的情况，分解
- 取模?

## 访存优化

- 对不写内存的函数调用，分析读的内存是否发生改变，消除多余的调用
- 如果局部变量的每个下标都只被赋值过一次，且赋值为常数，则将其提升为全局常量
- 拷贝传播：尽可能将读内存操作替换为上次读或写的值，对于数组没有被修改过的情况，将对数组的固定下标的访问替换为数组全局初始化的
  值
- 如果写操作不会对之后的读操作产生影响，则删除写操作

## SIMD 和多线程

- 多线程：对于一个k次迭代的循环，如果判断循环间无数据依赖，则将其分为num_threads块并行执行

- SIMD:如果一个循环中没有分支，且每次循环间无数据依赖，且每条指令都能换成SIMD指令，且SIMD寄存器够用，则替换为SIMD指令

## 指令调度

- 调整基本块内指令顺序，尽可能缩短变量的生存期，从而减少寄存器分配中的溢出
- Global Code Motion?

## 模拟

- 对于没有外部输入的程序，只保留输出语句和main的ret

## 参考网站

https://oi-wiki.org/lang/optimizations/

