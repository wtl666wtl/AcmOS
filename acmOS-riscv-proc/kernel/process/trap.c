#include <defs.h>
#include <riscv.h>
#include "process.h"
#include "syscall.h"
#include "memlayout.h"

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

extern void kernelvec();
extern int devintr();
void usertrapret();

extern char trampoline[], uservec[], userret[];

void trap_init_vec(){
    // Only 1 LoC
    // 将kernelvec作为内核中断处理基地址写入stvec向量。
    w_stvec((uint64)kernelvec);
}
// 真实的 syscall 处理过程
// 根据你在 user/stdlib.h 中的 syscall 操作在这里对应地寻找目标
extern uint64 sys_exit(void);
extern uint64 sys_putc(void);
extern uint64 sys_yield(void);
static uint64 (*syscalls[])(void) = {
        [SYS_EXIT]    sys_exit,
        [SYS_PUTC]    sys_putc,
        [SYS_YIELD]   sys_yield,
};

void syscall(){
    int num;
    struct thread *t = my_thread();

    num = t->trapframe->a7;
    if(num > 0 && num < NELEM(syscalls) && syscalls[num]){
        t->trapframe->a0 = syscalls[num]();
    } else {
        BUG("Unknown syscall");
        t->trapframe->a0 = -1;
    }
}

// 用户态的trap处理函数，内核态请参考 kernel/boot/start.c 中的 kernelvec
void usertrap(void) {
    int which_dev = 0;
    if ((r_sstatus() & SSTATUS_SPP) != 0) BUG("usertrap: not from user mode");
    // 由于中断处理过程可能仍然被中断，所以设置中断处理向量为 kernelvec（内核态处理）
    w_stvec((uint64)kernelvec);
    /* 你需要在这个函数中做的事情（仅供参考，你可以有自己的设计，如果不是2阶段中断处理）：
     * 
     * 保存用户态的pc
     * 判断发生trap的类型：syscall、设备中断、时钟中断等
     * 完成处理，进入到trap后半部分处理函数
     */

     struct thread *t = my_thread();
     //save user pc
     t->trapframe ->epc = r_sepc();

    if (r_scause() == 8) {
        // system call
        t->trapframe->epc += 4;
        intr_on();
        syscall();
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else {
        BUG_FMT("usertrap(): unexpected scause %p\n", r_scause());
        BUG_FMT("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    }

    // 处理时钟中断：重新调度
    if (which_dev == 2)yield();
    // 进入 trap 后半处理函数
    usertrapret();
}

// Trap 后半处理函数
void usertrapret() {
    /* 你需要在这个函数中做的事情（仅供参考，你可以有自己的设计）：
     * 
     * 关闭中断
     * 重新设置程序的中断处理向量
     * 还原目标寄存器
     * 设置下一次进入内核的一些参数
     * 切换页表
     * 跳转到二进制代码还原现场的部分
     */
    struct thread *t = my_thread();
    intr_off();

    w_stvec(TRAMPOLINE + (uservec - trampoline));

    t->trapframe->kernel_satp = r_satp();
    t->trapframe->kernel_sp = t->kstack + PGSIZE;
    t->trapframe->kernel_trap = (uint64)usertrap;
    t->trapframe->kernel_hartid = r_tp();

    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |= SSTATUS_SPIE;
    w_sstatus(x);

    w_sepc(t->trapframe->epc);

    uint64 satp = MAKE_SATP(t->pagetable);

    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}