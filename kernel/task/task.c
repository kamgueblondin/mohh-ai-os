#include "task.h"
#include "kernel/mem/pmm.h"
#include <stddef.h>
#include <stdint.h>

extern void context_switch(cpu_state_t* old_state, cpu_state_t* new_state);

volatile task_t* current_task = NULL;
volatile task_t* task_queue_head = NULL;
volatile uint32_t next_task_id = 0;

#define TASK_STACK_SIZE PAGE_SIZE // PAGE_SIZE doit être défini dans kernel/mem/pmm.h

void tasking_init() {
    asm volatile("cli");
    current_task = (task_t*)pmm_alloc_page();
    if (!current_task) {
        return;
    }
    current_task->id = next_task_id++;
    current_task->state = TASK_RUNNING;
    current_task->cpu_state.eip = 0;
    current_task->cpu_state.esp = 0;
    current_task->cpu_state.ebp = 0;
    current_task->cpu_state.eflags = 0;
    current_task->next = (struct task*)current_task;
    task_queue_head = current_task;
    asm volatile("sti");
}

task_t* create_task(void (*entry_point)()) {
    asm volatile("cli");
    task_t* new_task = (task_t*)pmm_alloc_page();
    if (!new_task) {
        asm volatile("sti");
        return NULL;
    }
    void* task_stack = pmm_alloc_page();
    if (!task_stack) {
        pmm_free_page(new_task);
        asm volatile("sti");
        return NULL;
    }
    new_task->id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->cpu_state.eflags = 0x202;
    new_task->cpu_state.eip = (uint32_t)entry_point;

    uint32_t* stack_ptr = (uint32_t*)((char*)task_stack + TASK_STACK_SIZE);
    *(--stack_ptr) = (uint32_t)entry_point;
    new_task->cpu_state.esp = (uint32_t)stack_ptr;
    new_task->cpu_state.ebp = 0;
    new_task->cpu_state.eax = 0;
    new_task->cpu_state.ebx = 0;
    new_task->cpu_state.ecx = 0;
    new_task->cpu_state.edx = 0;
    new_task->cpu_state.esi = 0;
    new_task->cpu_state.edi = 0;

    if (task_queue_head == NULL) {
        task_queue_head = new_task;
        new_task->next = new_task;
    } else {
        new_task->next = task_queue_head->next;
        task_queue_head->next = (struct task*)new_task;
    }
    asm volatile("sti");
    return new_task;
}

void schedule() {
    if (!current_task || current_task->next == current_task) {
        return;
    }
    task_t* last_task = (task_t*)current_task;
    task_t* next_task = (task_t*)current_task->next;
    last_task->state = TASK_READY;
    next_task->state = TASK_RUNNING;
    current_task = next_task;
    context_switch(&last_task->cpu_state, &next_task->cpu_state);
}
