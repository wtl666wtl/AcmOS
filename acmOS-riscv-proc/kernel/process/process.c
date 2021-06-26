#include "process.h"
#include "lock.h"
#include "pagetable.h"
#include "elf.h"
#include "memlayout.h"

extern const char binary_putc_start;
thread_t *running[NCPU];
struct context cpu_context[NCPU];
struct process proc[NPROC];
struct list_head sched_list[NCPU];
struct lock pidlock, tidlock, schedlock;
int _pid, _tid;

extern void swtch(struct context*, struct context*);

// ��ELF�ļ�ӳ�䵽����ҳ��ĵ�ַ�ռ䣬����pc����ֵ
// ���� ELF �ļ�����ο���https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-83432/index.html
static uint64 load_binary(pagetable_t *target_page_table, const char *bin){
	struct elf_file *elf;
    int i;
    uint64 seg_sz, p_vaddr, seg_map_sz;
	elf = elf_parse_file(bin);
	
	/* load each segment in the elf binary */
	for (i = 0; i < elf->header.e_phnum; ++i) {
		if (elf->p_headers[i].p_type == PT_LOAD) {
            // ���� ELF �ļ���ʽ����ӳ��
            // ��ELF�л����һ�εĶδ�С
            seg_sz = elf->p_headers[i].p_memsz;
            // ��Ӧ�ε����ڴ��е������ַ
            p_vaddr = elf->p_headers[i].p_vaddr;
            // ��ӳ���С��ҳ����
			seg_map_sz = ROUNDUP(seg_sz + p_vaddr, PGSIZE) - PGROUNDDOWN(p_vaddr);
            // ���������������Ŀ�ģ����������ӳ��/���Ƶ���Ӧ���ڴ�ռ�
            // һ�ֿ��ܵ�ʵ�����£�
            /* 
             * �� target_page_table �з���һ���С
             * ͨ�� memcpy ��ĳһ�θ��ƽ�����һ��ռ�
             * ҳ��ӳ���޸�
             */
		}
	}
	/* PC: the entry point */
	return elf->header.e_entry;
}

int alloc_tid(){
    int tid;

    acquire(&tidlock);
    tid = _tid;
    _tid = _tid + 1;
    release(&tidlock);

    return tid;
}

int alloc_pid(){
    int pid;

    acquire(&pidlock);
    pid = _pid;
    _pid = _pid + 1;
    release(&pidlock);

    return pid;
}

/* ����һ�����̣���Ҫ�����������Ŀ�꣺
 * 
 * ����һ�����߳�
 * ����һ�Ž���ҳ��
 * ����pid��tid
 * ���ó�ʼ���߳�������
 * ���ó�ʼ���̷߳��ص�ַ�Ĵ���ra��ջ�Ĵ���sp
 * 
 * ��������������Ϊһ�������ƵĴ����һ���߳�ָ��(���崫���������Լ��޸�)
 * ��������״ν����û�̬֮ǰ��Ӧ�����ú�trap��������Ϊusertrap���������Զ���ģ�
 */
process_t *alloc_proc(const char* bin, thread_t *thr) {
    struct process *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->process_state == UNUSED) {
            p->pid = alloc_pid();
            p->process_state = RUNNABLE;

            thr->trapframe = (struct trapframe *)mm_kalloc();

            //p->pagetable = //;//?

            memset(&thr->context, 0, sizeof(thr->context));
            //thr->context.ra = (uint64);
            thr->context.sp = thr->kstack + PGSIZE;

            return p;
        } else {
            release(&p->lock);
        }
    }
    thr = NULL;
    return NULL;
}

bool load_thread(file_type_t type){
    if(type == PUTC){
        thread_t *t = NULL;
        process_t *p = alloc_proc(&binary_putc_start, t);
        if(!t) return false;
        sched_enqueue(t);
    } else {
        BUG("Not supported");
    }

}

// sched_enqueue��sched_dequeue����Ҫ�����Ǽ���һ�����񵽶����к�ɾ��һ������
// ������������չʾ�����ʹ��list.h�пɵĺ��������롢ɾ�����жϿա�ȡԪ�أ�
// ������Բο���Stackoverflow�ϵĻش�
// https://stackoverflow.com/questions/15832301/understanding-container-of-macro-in-the-linux-kernel
void sched_enqueue(thread_t *target_thread){
    if(target_thread->thread_state == RUNNING) BUG("Running Thread cannot be scheduled.");
    list_add(&target_thread->sched_list_thread_node, &(sched_list[cpuid()]));
}

thread_t *sched_dequeue(){
    if(list_empty(&(sched_list[cpuid()]))) BUG("Scheduler List is empty");
    thread_t *head = container_of(&(sched_list[cpuid()]), thread_t, sched_list_thread_node);
    list_del(&head->sched_list_thread_node);
    return head;
}

bool sched_empty(){
    return list_empty(&(sched_list[cpuid()]));
}

// ��ʼ����ĳ���ض��ĺ���
void thread_run(thread_t *target){
    acquire(&target->lock);
    if(target->thread_state != RUNNABLE)BUG("target isn't runnable!");
    target->thread_state = RUNNING;
    running[cpuid()] = target;
    swtch(&target->context, &cpu_context[cpuid()]);
    running[cpuid()] = 0;
    release(&target->lock);
}

// sched_start�����������ȣ����յ��ȵĶ��п�ʼ���С�
void sched_start(){
    while(1){
        if(sched_empty()) BUG("Scheduler list empty, no app loaded");
        thread_t *next = sched_dequeue();
        thread_run(next);
    }
}

void sched_init(){
    // ��ʼ�����ȶ�����
    lock_init(&schedlock);
    // ��ʼ������ͷ
    init_list_head(&(sched_list[cpuid()]));
}

void proc_init(){
    // ��ʼ��pid��tid��
    lock_init(&pidlock);
    lock_init(&tidlock);
    // ����������������Ŀ�ģ�ӳ���һ���û��̲߳��Ҳ�����ȶ���
    if(!load_thread(PUTC)) BUG("Load failed");
}

struct thread* my_thread(){
    return running[cpuid()];
}

void yield(void){
    struct thread *t = my_thread();
    acquire(&t->lock);
    t->thread_state = RUNNABLE;
    sched_enqueue(t);
    swtch(&t->context, &cpu_context[cpuid()]);
    release(&t->lock);
}



