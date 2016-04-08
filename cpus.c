/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Needed early for CONFIG_BSD etc. */
#include "config-host.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"

#include "uc_priv.h"

static bool cpu_can_run(CPUState *cpu);
static bool tcg_exec_all(struct uc_struct *uc);
static int qemu_tcg_init_vcpu(CPUState *cpu);
static void qemu_tcg_cpu_loop(struct uc_struct *uc);

int vm_start(struct uc_struct *uc)
{
    if (resume_all_vcpus(uc)) {
        return -1;
    }
    return 0;
}

bool cpu_is_stopped(CPUState *cpu)
{
    return cpu->stopped;
}

// TODO: use uc_struct?
void hw_error(const char *fmt, ...)
{
    va_list ap;
    // CPUState *cpu;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    // fprintf(stderr, "CPU #%d:\n", cpu->cpu_index);
    // cpu_dump_state(cpu, stderr, fprintf, CPU_DUMP_FPU);
    va_end(ap);
    abort();
}

static bool cpu_can_run(CPUState *cpu)
{
    if (cpu->stop) {
        return false;
    }
    if (cpu_is_stopped(cpu)) {
        return false;
    }
    return true;
}

static void qemu_tcg_cpu_loop(struct uc_struct *uc)
{
    CPUState *cpu = arg;

    qemu_mutex_lock(&uc->qemu_global_mutex);
    cpu->thread_id = qemu_get_thread_id();
    cpu->created = true;
    qemu_cond_signal(&qemu_cpu_cond);

    while (1) {
        if (tcg_exec_all())
            break;
    }

    cpu->thread_id = 0;
    cpu->created = false;
    qemu_mutex_unlock(&uc->qemu_global_mutex);

}

void pause_all_vcpus(struct uc_struct *uc)
{
    CPUState *cpu;
    qemu_thread_join(uc->cpu->thread);
}

void cpu_resume(CPUState *cpu)
{
    cpu->stop = false;
    cpu->stopped = false;
    qemu_cpu_kick(cpu);
}

void resume_all_vcpus(struct uc_struct *uc)
{
    CPUState *cpu = uc->cpu;
    if (! cpu->created) {
        cpu->created = true;
        cpu->stopped = true;
        cpu->halted = 0;
        qemu_tcg_init_vcpu(cpu);
    }
    qemu_thread_get_self(uc, cpu->thread);
    cpu_resume(cpu);
    qemu_tcg_cpu_loop(uc);
}

static void qemu_tcg_init_vcpu(CPUState *cpu)
{
    struct uc_struct *uc = cpu->uc;
    tcg_cpu_address_space_init(cpu, cpu->as);
    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
}

static bool tcg_exec_all(struct uc_struct *uc)
{
    int r;
    bool finish = false;
    CPUState *cpu = uc->cpu;
    CPUArchState *env = cpu->env_ptr;
    while (!uc->exit_request) {
        if (cpu_can_run(cpu)) {
            uc->reset_request = false;
            r = cpu_exec(uc, env);

            // reset == quit current TB but continue emulating
            if (uc->reset_request) {
                uc->stop_request = false;
            } else if (uc->stop_request) {
                finish = true;
                break;
            }

            // save invalid memory access error & quit
            if (env->invalid_error) {
                uc->invalid_addr = env->invalid_addr;
                uc->invalid_error = env->invalid_error;
                finish = true;
                break;
            }
            if (r == EXCP_DEBUG) {
                cpu->stopped = true;
                break;
            }
            if (r == EXCP_HLT) {
                finish = true;
                break;
            }
        } else if (cpu->stop || cpu->stopped) {
            // TODO: can this ever actually happen?
            printf(">>> got stopped!!!\n");
            break;
        }
    }
    uc->exit_request = false;
    return finish;
}
