// 实现不同平台的上下文
/* 
x86_64架构
    rip/rsp：x86_64的指令指针和栈指针（对应RIP和RSP寄存器） 
    rbx/rbp/r12-r15：x86_64的通用寄存器，属于Callee-saved寄存器（调用者需保存的寄存器） 
    xmm6_15：XMM寄存器（用于SSE/浮点运算），x86_64架构中XMM6-XMM15为调用者保存寄存器 
ARM64架构
    pc/sp：程序计数器和栈指针（对应PC和SP寄存器）。
    x19-x28：ARM64的Callee-saved寄存器，需在函数调用时保存 

*/

/*
windows约定
    函数参数从左到右：rcx、rdx、r8、r9
linux约定
    rdi、rsi、rdx、rcx、r8、r9
*/



#pragma once
#include <cstdint>
#include <xmmintrin.h> // For __m128i

// 上下文结构体（需严格匹配汇编偏移）
// rbx、rbp、rsi、rdi r12-15 xmm6-15
struct Context {
#if defined(__x86_64__)
    void* rip;
    void* rsp;
    void* rbx;
    void* rbp;
    void* rsi;
    void* rdi;
    void* r12;
    void* r13;
    void* r14;
    void* r15;
    __m128i xmm6;
    __m128i xmm7;
    __m128i xmm8;
    __m128i xmm9;
    __m128i xmm10;
    __m128i xmm11;
    __m128i xmm12;
    __m128i xmm13;
    __m128i xmm14;
    __m128i xmm15;  //EO

    void* ptr; //传参数指针 F0
    void* funcPtr; //绑定的函数 0:主协程 F8
    void* firstIn; // 1:第一次传入，需要构建函数；0:第二次传入 100
#elif defined(__aarch64__)
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;  // x29
    uint64_t lr;   // x30
    void* sp;
    void* pc;
#endif
};
// 保存上下文
// 恢复到另一个上下文
//


extern "C" {
    void ctx_save(Context* ctx);  //保存上下文，恢复时从下一个指令开始执行
    void ctx_swap(Context* o_ctx,Context* t_ctx); //保存上下文到o_ctx，恢复到t_ctx,返回时o_ctx从下一条开始执行
}



#if defined(__x86_64__)

// x86_64实现
// rdi是第一个参数，保存上下文结构体地址
void ctx_save(Context* ctx) {
    asm volatile (
        //rcx=&ctx
        // 保存返回地址到rip字段
        "movq 8(%rbp), %rax\n"      // 保存函数返回地址到rax
        "movq %rax, 0x00(%rcx)\n"      // ctx->rip = return address
        
        // 保存原栈指针
        "leaq 0x10(%rsp), %rax\n"
        "movq %rax, 0x08(%rcx)\n"      // ctx->rsp
        
        // 保存Callee-saved寄存器
        "movq %rbx, 0x10(%rcx)\n"

        "movq 0(%rbp), %rax\n" //由于函数压入返回地址、rbp
        "movq %rax, 0x18(%rcx)\n"

        "movq %rsi, 0x20(%rcx)\n"
        "movq %rdi, 0x28(%rcx)\n"
        "movq %r12, 0x30(%rcx)\n"
        "movq %r13, 0x38(%rcx)\n"
        "movq %r14, 0x40(%rcx)\n"
        "movq %r15, 0x48(%rcx)\n"
        
        // 保存XMM6-XMM15
        "movdqu %xmm6, 0x50(%rcx)\n"
        "movdqu %xmm7, 0x60(%rcx)\n"
        "movdqu %xmm8, 0x70(%rcx)\n"
        "movdqu %xmm9, 0x80(%rcx)\n"
        "movdqu %xmm10, 0x90(%rcx)\n"
        "movdqu %xmm11, 0xA0(%rcx)\n"
        "movdqu %xmm12, 0xB0(%rcx)\n"
        "movdqu %xmm13, 0xC0(%rcx)\n"
        "movdqu %xmm14, 0xD0(%rcx)\n"
        "movdqu %xmm15, 0xE0(%rcx)\n"
    );
}

void ctx_swap(Context* o_ctx,Context* t_ctx){
    asm(
        // rcx=o_ctx, rdx=t_ctx
        
        // 保存当前上下文到o_ctx-------------------------------------------//
        "movq 8(%rbp), %rax\n"      // 保存函数返回地址到rax
        "movq %rax, 0x00(%rcx)\n"      // ctx->rip = return address
        
        // 保存原栈指针
        "leaq 0x10(%rsp), %rax\n"
        "movq %rax, 0x08(%rcx)\n"      // ctx->rsp
        
        // 保存Callee-saved寄存器
        "movq %rbx, 0x10(%rcx)\n"

        "movq 0(%rbp), %rax\n" //由于函数压入返回地址、rbp
        "movq %rax, 0x18(%rcx)\n"

        "movq %rsi, 0x20(%rcx)\n"
        "movq %rdi, 0x28(%rcx)\n"
        "movq %r12, 0x30(%rcx)\n"
        "movq %r13, 0x38(%rcx)\n"
        "movq %r14, 0x40(%rcx)\n"
        "movq %r15, 0x48(%rcx)\n"
        
        // 保存XMM6-XMM15
        "movdqu %xmm6, 0x50(%rcx)\n"
        "movdqu %xmm7, 0x60(%rcx)\n"
        "movdqu %xmm8, 0x70(%rcx)\n"
        "movdqu %xmm9, 0x80(%rcx)\n"
        "movdqu %xmm10, 0x90(%rcx)\n"
        "movdqu %xmm11, 0xA0(%rcx)\n"
        "movdqu %xmm12, 0xB0(%rcx)\n"
        "movdqu %xmm13, 0xC0(%rcx)\n"
        "movdqu %xmm14, 0xD0(%rcx)\n"
        "movdqu %xmm15, 0xE0(%rcx)\n"
        
        // 恢复目标上下文t_ctx------------------------------------------//
        // 恢复XMM寄存器
        "movdqu 0x50(%rdx), %xmm6\n"
        "movdqu 0x60(%rdx), %xmm7\n"
        "movdqu 0x70(%rdx), %xmm8\n"
        "movdqu 0x80(%rdx), %xmm9\n"
        "movdqu 0x90(%rdx), %xmm10\n"
        "movdqu 0xA0(%rdx), %xmm11\n"
        "movdqu 0xB0(%rdx), %xmm12\n"
        "movdqu 0xC0(%rdx), %xmm13\n"
        "movdqu 0xD0(%rdx), %xmm14\n"
        "movdqu 0xE0(%rdx), %xmm15\n"
        
        // 恢复通用寄存器
        "movq 0x48(%rdx), %r15\n"
        "movq 0x40(%rdx), %r14\n"
        "movq 0x38(%rdx), %r13\n"
        "movq 0x30(%rdx), %r12\n"
        "movq 0x28(%rdx), %rdi\n"
        "movq 0x20(%rdx), %rsi\n"
        "movq 0x18(%rdx), %rbp\n"
        "movq 0x10(%rdx), %rbx\n"
        

        //恢复执行流----------------------------------------//
        "movq 0x100(%rdx), %rax\n"
        "cmpq $1, %rax\n"
        "jne .L1\n"// 如果不是第一次进入

        // 第一次进入时，准备工作函数，参数放到rcx

        "movq 0xF0(%rdx), %rcx\n" //准备好参数到rcx
        "movq $0, 0x100(%rdx)\n" //下次进入不用重新进入函数
        "movq 0xF8(%rdx), %rax\n"//准备好函数地址
        "movq 0x08(%rdx), %rsp\n"
        "jmp *%rax\n" //跳转到工作函数

        // 否则，恢复栈指针和跳转地址
        ".L1:\n"
        "movq 0x08(%rdx), %rsp\n"
        "movq 0x00(%rdx), %rax\n"
        "jmp *%rax\n"               // 跳转到保存的rip
    );
}
int ctx_make(Context& ctx,void* func,void* ptr=0,size_t stack_size=1024*1024){
    char* stack_bottom = new char[stack_size];
    char* stack_top = stack_bottom + stack_size - 8;
    ctx.rsp = (void*)stack_top;
    ctx.rbp = ctx.rsp;
    ctx.firstIn = (void*)1;
    ctx.funcPtr = func;
    ctx.ptr = ptr;
    return 0;
}

#elif defined(__aarch64__)

// ARM64实现
__asm__(
    ".global ctx_save\n"
    "ctx_save:\n"
    // 保存Callee-saved寄存器
    "str x19, [x0, #0x00]\n"    // ctx->x19
    "str x20, [x0, #0x08]\n"    // ctx->x20
    "str x21, [x0, #0x10]\n"
    "str x22, [x0, #0x18]\n"
    "str x23, [x0, #0x20]\n"
    "str x24, [x0, #0x28]\n"
    "str x25, [x0, #0x30]\n"
    "str x26, [x0, #0x38]\n"
    "str x27, [x0, #0x40]\n"
    "str x28, [x0, #0x48]\n"    // ctx->x28
    // 保存Frame Pointer和Link Register
    "str x29, [x0, #0x50]\n"    // ctx->fp
    "str x30, [x0, #0x58]\n"    // ctx->lr
    // 保存栈指针和程序计数器
    "mov x1, sp\n"
    "str x1, [x0, #0x60]\n"     // ctx->sp
    "adr x1, .ret_addr\n"       // 获取当前PC
    "str x1, [x0, #0x68]\n"     // ctx->pc
    // 返回0
    "mov x0, #0\n"
    ".ret_addr:\n"
    "ret\n"
);

__asm__(
    ".global ctx_restore\n"
    "ctx_restore:\n"
    // 恢复栈指针和程序计数器
    "ldr sp, [x0, #0x60]\n"
    "ldr x1, [x0, #0x68]\n"
    // 恢复Frame Pointer和Link Register
    "ldr x29, [x0, #0x50]\n"
    "ldr x30, [x0, #0x58]\n"
    // 恢复Callee-saved寄存器
    "ldr x28, [x0, #0x48]\n"
    "ldr x27, [x0, #0x40]\n"
    "ldr x26, [x0, #0x38]\n"
    "ldr x25, [x0, #0x30]\n"
    "ldr x24, [x0, #0x28]\n"
    "ldr x23, [x0, #0x20]\n"
    "ldr x22, [x0, #0x18]\n"
    "ldr x21, [x0, #0x10]\n"
    "ldr x20, [x0, #0x08]\n"
    "ldr x19, [x0, #0x00]\n"
    // 跳转到保存的程序计数器地址
    "br x1\n"
);

#endif    