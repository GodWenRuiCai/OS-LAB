/* Notes written by Qian Liu <qianlxc@outlook.com>
  If you find any bug, please contact with me.*/

#include <mmu.h>
#include <error.h>
#include <env.h>
#include <kerelf.h>
#include <sched.h>
#include <pmap.h>
#include <printf.h>

struct Env *envs = NULL;                // All environments
struct Env *curenv = NULL;              // the current env

static struct Env_list env_free_list;   // Free list
struct Env_list env_sched_list[2];      // Runnable list

extern Pde *boot_pgdir;
extern char *KERNEL_SP;


/* Overview:
 *  This function is for making an unique ID for every env.
 *
 * Pre-Condition:
 *  Env e is exist.
 *
 * Post-Condition:
 *  return e's envid on success.
 */

u_int mkenvid(struct Env *e)
{
    static u_long next_env_id = 0;

    /*Hint: lower bits of envid hold e's position in the envs array. */
    u_int idx = e - envs;

    /*Hint:  high bits of envid hold an increasing number. */
    return (++next_env_id << (1 + LOG2NENV)) | idx;

}
/* Overview:
 *  Converts an envid to an env pointer.
 *  If envid is 0 , set *penv = curenv;otherwise set *penv = envs[ENVX(envid)];
 *
 * Pre-Condition:
 *  Env penv is exist,checkperm is 0 or 1.
 *
 * Post-Condition:
 *  return 0 on success,and sets *penv to the environment.
 *  return -E_BAD_ENV on error,and sets *penv to NULL.
 */
int envid2env(u_int envid, struct Env **penv, int checkperm)
{
    struct Env *e;
    /* Hint:
 *      *  If envid is zero, return the current environment.*/
    /*Step 1: Assign value to e using envid. */
    if (envid == 0) {
        *penv = curenv;
        return 0;
    }
    e = envs + ENVX(envid);
    if (e->env_status == ENV_FREE || e->env_id != envid) {
            *penv = 0;
            return -E_BAD_ENV;
    }
    /* Hint:
       *  Check that the calling environment has legitimate permissions
       *  to manipulate the specified environment.
       *  If checkperm is set, the specified environment
       *  must be either the current environment.
       *  or an immediate child of the current environment.If not, error! */
    /*Step 2: Make a check according to checkperm. */
    if (checkperm) {
        if (!(e == curenv || e->env_parent_id == curenv->env_id)) {
            *penv = 0;
            return -E_BAD_ENV;
        }
    }
    *penv = e;
    return 0;
}

/* Overview:
 *  Mark all environments in 'envs' as free and insert them into the env_free_list.
 *  Insert in reverse order,so that the first call to env_alloc() return envs[0].
 *
 * Hints:
 *  You may use these defines to make it:
 *      LIST_INIT,LIST_INSERT_HEAD
 */
void
env_init(void)
{
    int i;
    /*Step 1: Initial env_free_list. */
    LIST_INIT(&env_free_list);
        LIST_INIT(&env_sched_list[0]);
        LIST_INIT(&env_sched_list[1]);

    for (i=NENV-1;i>=0;i--) {
        envs[i].env_status = ENV_FREE;
        LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
    }

    /*Step 2: Travel the elements in 'envs', init every element(mainly initial its status, mark it as free)
     * and inserts them into the env_free_list as reverse order. */
}


/* Overview:
 *  Initialize the kernel virtual memory layout for environment e.
 *  Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
 *  and initialize the kernel portion of the new environment's address space.
 *  Do NOT map anything into the user portion of the environment's virtual address space.
 */
/***Your Question Here***/
static int
env_setup_vm(struct Env *e)
{

    int i, r;
    struct Page *p = NULL;
    Pde *pgdir;
    /*Step 1: Allocate a page for the page directory using a function you completed in the lab2.
       * and add its reference.
       *pgdir is the page directory of Env e, assign value for it. */
    if ((r = page_alloc(&p)) < 0) {
                panic("env_setup_vm - page alloc error\n");
        return r;
    }
        p->pp_ref ++;
    pgdir = (Pde *)page2kva(p);
    /*Step 2: Zero pgdir's field before UTOP. */
    for (i = 0;i < PDX(UTOP);i++) {
        pgdir[i] = 0;
    }
    /*Step 3: Copy kernel's boot_pgdir to pgdir. */
    /* Hint:
     *  The VA space of all envs is identical above UTOP
     *  (except at VPT and UVPT, which we've set below).
     *  See ./include/mmu.h for layout.
     *  Can you use boot_pgdir as a template?
     */
    for (i = PDX(UTOP);i < 1024;i++) {
        if (i!=PDX(VPT) && i != PDX(UVPT))
            pgdir[i] = boot_pgdir[i];
    }

    /*Step 4: Set e->env_pgdir and e->env_cr3 accordingly. */

    e->env_pgdir = pgdir;
    e->env_cr3 = PADDR(pgdir);

    /*VPT and UVPT map the env's own page table, with
       *different permissions. */
    e->env_pgdir[PDX(VPT)]   = e->env_cr3;
    e->env_pgdir[PDX(UVPT)]  = e->env_cr3 | PTE_V | PTE_R;
    return 0;
}

/* Overview:
 *  Allocates and Initializes a new environment.
 *  On success, the new environment is stored in *new.
 *
 * Pre-Condition:
 *  If the new Env doesn't have parent, parent_id should be zero.
 *  env_init has been called before this function.
 *
 * Post-Condition:
 *  return 0 on success, and set appropriate values for Env new.
 *  return -E_NO_FREE_ENV on error, if no free env.
 *
 * Hints:
 *  You may use these functions and defines:
 *      LIST_FIRST,LIST_REMOVE,mkenvid (Not All)
 *  You should set some states of Env:
 *      id , status , the sp register, CPU status , parent_id
 *      (the value of PC should NOT be set in env_alloc)
 */

int
env_alloc(struct Env **new, u_int parent_id)
{
    int r;
    struct Env *e;
    /*Step 1: Get a new Env from env_free_list*/

    e = LIST_FIRST(&env_free_list);
    if (e == NULL) {
                //printf("alloc env failed\n");
        return -E_NO_FREE_ENV;
    }
    /*Step 2: Call certain function(has been implemented) to init kernel memory layout for this new Env.
     *The function mainly maps the kernel address to this new Env address. */
    env_setup_vm(e);
    /*Step 3: Initialize every field of new Env with appropriate values*/
    e->env_id = mkenvid(e);
    e->env_status = ENV_RUNNABLE;
    e->env_parent_id = parent_id;

    /*Step 4: focus on initializing env_tf structure, located at this new Env.
     * especially the sp register,CPU status. */
    e->env_tf.cp0_status = 0x10001004;
    e->env_tf.regs[29] = USTACKTOP;

    /*Step 5: Remove the new Env from Env free list*/
    *new = e;
        LIST_REMOVE(e, env_link);
    return 0;
}

/* Overview:
 *   This is a call back function for kernel's elf loader.
 * Elf loader extracts each segment of the given binary image.
 * Then the loader calls this function to map each segment
 * at correct virtual address.
 *
 *   `bin_size` is the size of `bin`. `sgsize` is the
 * segment size in memory.
 *
 * Pre-Condition:
 *   bin can't be NULL.
 *   Hint: va may NOT aligned 4KB.
 *
 * Post-Condition:
 *   return 0 on success, otherwise < 0.
 */
/*
概述：
   这是内核的elf加载器的回调函数。
Elf加载器提取给定二进制图像的每个片段。
然后加载器调用此函数来映射每个段
在正确的虚拟地址。
   `bin_size`是`bin`的大小。 `sgsize`是
内存中的段大小。
前提：
   bin不能为NULL。
   提示：va可能不对齐4KB。
后置条件：
   成功时返回0，否则<0。
 */
/*
load_elf() 函数会从 ELF 文件文件中解析出每个 segment 的四个信息：
va(该段需要被加载到的虚地址)、
sgsize(该段在内存中的大小)、
bin(该段在 ELF 文件中 的内容)、
bin_size(该段在文件中的大小)，
并将这些信息传给我们的“自定义函数”。
 */
static int load_icode_mapper(u_long va, u_int32_t sgsize,
                        u_char *bin, u_int32_t bin_size, void *user_data)
{
    struct Env *env = (struct Env *)user_data;    //why??????
    struct Page *p = NULL;
    u_long i;
    int r;
    u_long offset = va - ROUNDDOWN(va, BY2PG);     //超出的那一部分
    /*Step 1: load all content of bin into memory. */

    if (bin == NULL)
        return -1;

    for (i = 0; i < bin_size; i += BY2PG) {   //给本段分配物理页
        /* Hint: You should alloc a page and increase the reference count of it. */
        if (page_alloc(&p) < 0) {
                        return -1;
                }
        //p->pp_ref ++;

        //每分配一个物理页，将ELF文件的内容复制上去
        //注意到mmu.h里面的bcopy函数
        //注意va不对齐4KB,而page一定是对齐4KB的，这里在第一页将其解决
        //在page_alloc中页面已经初始化为0
        if (i == 0) {
            if ((BY2PG-offset)<bin_size-i)
                r = BY2PG - offset;
            else
                r = bin_size - i;
            bcopy(bin,(char *)(page2kva(p) + offset),r);
        }
        else {
            if (BY2PG<bin_size-i)
                r = BY2PG;
            else
                r = bin_size - i;
            bcopy(bin + i - offset,(char *)page2kva(p),r);
        }

        if (page_insert(env->env_pgdir, p, va + i, PTE_R|PTE_V) < 0) {
                        return -1;
        }
    }
    /*Step 2: alloc pages to reach `sgsize` when `bin_size` < `sgsize`.
        * i has the value of `bin_size` now. */
    while (i < sgsize) {
        if (page_alloc(&p) < 0) {
                        return -1;
                }
        //p->pp_ref ++;
        if (page_insert(env->env_pgdir, p, va + i, PTE_R|PTE_V) < 0) {
                        return -1;
                }
        i += BY2PG;
    }
    return 0;
}

/* Overview:
 *  Sets up the the initial stack and program binary for a user process.
 *  This function loads the complete binary image by using elf loader,
 *  into the environment's user memory. The entry point of the binary image
 *  is given by the elf loader. And this function maps one page for the
 *  program's initial stack at virtual address USTACKTOP - BY2PG.
 *
 * Hints:
 *  All mappings are read/write including those of the text segment.
 *  You may use these :
 *      page_alloc, page_insert, page2kva , e->env_pgdir and load_elf.
 */
static void
load_icode(struct Env *e, u_char *binary, u_int size)
{
    /* Hint:
     *  You must figure out which permissions you'll need
     *  for the different mappings you create.
     *  Remember that the binary image is an a.out format image,
     *  which contains both text and data.
     */
    struct Page *p = NULL;
    u_long entry_point;
    u_long r;
    u_long perm;

    /*Step 1: alloc a page. */
    if (page_alloc(&p) < 0)
        return ;
    //p->pp_ref ++;
    /*Step 2: Use appropriate perm to set initial stack for new Env. */
    /*Hint: The user-stack should be writable? */
    perm = PTE_V | PTE_R;
    page_insert(e->env_pgdir, p, USTACKTOP - BY2PG, perm);

    /*Step 3:load the binary by using elf loader. */
    if (load_elf(binary, size, &entry_point, e, load_icode_mapper) < 0)
        return ;
    /***Your Question Here***/
    /*Step 4:Set CPU's PC register as appropriate value. */
    e->env_tf.pc = entry_point;
}

/* Overview:
 *  Allocates a new env with env_alloc, loads the named elf binary into
 *  it with load_icode and then set its priority value. This function is
 *  ONLY called during kernel initialization, before running the first
 *  user_mode environment.
 *
 * Hints:
 *  this function wrap the env_alloc and load_icode function.
 */
void
env_create_priority(u_char *binary, int size, int priority)
{
    struct Env *e;
    /*Step 1: Use env_alloc to alloc a new env. */
    if (env_alloc(&e, 0) < 0)
        return ;
    /*Step 2: assign priority to the new env. */
    e->env_pri = priority;
    /*Step 3: Use load_icode() to load the named elf binary. */
    load_icode(e, binary, size);
        LIST_INSERT_HEAD(&env_sched_list[0], e, env_sched_link);
}
/* Overview:
 * Allocates a new env with default priority value.
 *
 * Hints:
 *  this function warp the env_create_priority function/
 */

void
env_create(u_char *binary, int size)
{
    /*Step 1: Use env_create_priority to alloc a new env with priority 1 */
    env_create_priority(binary, size, 1);

}
/* Overview:
 *  Frees env e and all memory it uses.
 */
void
env_free(struct Env *e)
{
    Pte *pt;
    u_int pdeno, pteno, pa;

    /* Hint: Note the environment's demise.*/
    printf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

    /* Hint: Flush all mapped pages in the user portion of the address space */
    for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
    /* Hint: only look at mapped page tables. */
        if (!(e->env_pgdir[pdeno] & PTE_V)) {
            continue;
        }
        /* Hint: find the pa and va of the page table. */
        pa = PTE_ADDR(e->env_pgdir[pdeno]);
        pt = (Pte *)KADDR(pa);
        /* Hint: Unmap all PTEs in this page table. */
        for (pteno = 0; pteno <= PTX(~0); pteno++)
            if (pt[pteno] & PTE_V) {
                page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
            }
        /* Hint: free the page table itself. */
            e->env_pgdir[pdeno] = 0;
            page_decref(pa2page(pa));
        }
    /* Hint: free the page directory. */
    pa = e->env_cr3;
    e->env_pgdir = 0;
    e->env_cr3 = 0;
    page_decref(pa2page(pa));
    /* Hint: return the environment to the free list. */
    e->env_status = ENV_FREE;
    LIST_INSERT_HEAD(&env_free_list, e, env_link);
    LIST_REMOVE(e, env_sched_link);
}

/* Overview:
 *  Frees env e, and schedules to run a new env
 *  if e is the current env.
 */
void
env_destroy(struct Env *e)
{
    /* Hint: free e. */
    env_free(e);

    /* Hint: schedule to run a new environment. */
    if (curenv == e) {
            curenv = NULL;
    /* Hint:Why this? */
            bcopy((void *)KERNEL_SP - sizeof(struct Trapframe),
                      (void *)TIMESTACK - sizeof(struct Trapframe),
                      sizeof(struct Trapframe));
            printf("i am killed ... \n");
            sched_yield();
    }
}

extern void env_pop_tf(struct Trapframe *tf, int id);
extern void lcontext(u_int contxt);

/* Overview:
 *  Restores the register values in the Trapframe with the
 *  env_pop_tf, and context switch from curenv to env e.
 *
 * Post-Condition:
 *  Set 'e' as the curenv running environment.
 *
 * Hints:
 *  You may use these functions:
 *      env_pop_tf and lcontext.
 */
void
env_run(struct Env *e)
{
    /*Step 1: save register state of curenv. */
    /* Hint: if there is a environment running,you should do
    *  context switch.You can imitate env_destroy() 's behaviors.*/
    //if (curenv == e || e == NULL) {
    //    return ;
    //} //此时显然不需要启动进程e

    if (curenv) {
        struct Trapframe *old;
        old = (struct Trapframe *)(TIMESTACK - sizeof(struct Trapframe));
        bcopy(old, &(curenv->env_tf), sizeof(struct Trapframe));
        curenv->env_tf.pc = old->cp0_epc;
    }

    /*Step 2: Set 'curenv' to the new environment. */
    curenv = e;

    /*Step 3: Use lcontext() to switch to its address space. */
    lcontext(KADDR(curenv->env_cr3));
    /*Step 4: Use env_pop_tf() to restore the environment's
     * environment   registers and drop into user mode in the
     * the   environment.
     */
    /* Hint: You should use GET_ENV_ASID there.Think why? */
    env_pop_tf(&(curenv -> env_tf), GET_ENV_ASID(curenv->env_id));
}
void env_check()
{
    struct Env *temp, *pe, *pe0, *pe1, *pe2;
    struct Env_list fl;
    int re = 0;
    // should be able to allocate three envs
    pe0 = 0;
    pe1 = 0;
    pe2 = 0;
    assert(env_alloc(&pe0, 0) == 0);
    assert(env_alloc(&pe1, 0) == 0);
    assert(env_alloc(&pe2, 0) == 0);
    assert(pe0);
    assert(pe1 && pe1 != pe0);
    assert(pe2 && pe2 != pe1 && pe2 != pe0);
    // temporarily steal the rest of the free envs
    fl = env_free_list;
    // now this env_free list must be empty!!!!
    LIST_INIT(&env_free_list);
    // should be no free memory
     assert(env_alloc(&pe, 0) == -E_NO_FREE_ENV);
    // recover env_free_list
    env_free_list = fl;
    printf("pe0->env_id %d\n",pe0->env_id);
    printf("pe1->env_id %d\n",pe1->env_id);
    printf("pe2->env_id %d\n",pe2->env_id);
    assert(pe0->env_id == 2048);
    assert(pe1->env_id == 4097);
    assert(pe2->env_id == 6146);
    printf("env_init() work well!\n");
     /* check envid2env work well */
     pe2->env_status = ENV_FREE;
    re = envid2env(pe2->env_id, &pe, 0);
    assert(pe == 0 && re == -E_BAD_ENV);
    pe2->env_status = ENV_RUNNABLE;
    re = envid2env(pe2->env_id, &pe, 0);
    assert(pe->env_id == pe2->env_id &&re == 0);
    temp = curenv;
    curenv = pe0;
    re = envid2env(pe2->env_id, &pe, 1);
    assert(pe == 0 && re == -E_BAD_ENV);
    curenv = temp;
    printf("envid2env() work well!\n");
    /* check env_setup_vm() work well */
    printf("pe1->env_pgdir %x\n",pe1->env_pgdir);
    printf("pe1->env_cr3 %x\n",pe1->env_cr3);
    assert(pe2->env_pgdir[PDX(UTOP)] == boot_pgdir[PDX(UTOP)]);
    assert(pe2->env_pgdir[PDX(UTOP)-1] == 0);
    printf("env_setup_vm passed!\n");
    assert(pe2->env_tf.cp0_status == 0x10001004);
    printf("pe2`s sp register %x\n",pe2->env_tf.regs[29]);
    printf("env_check() succeeded!\n");
}

