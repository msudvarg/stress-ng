/*
 * Copyright (C)      2022 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef CORE_CACHE_H
#define CORE_CACHE_H

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_CLFLUSH)

typedef void (*shim_clflush_func_t)(volatile void *ptr);

static inline void ALWAYS_INLINE shim_clflush_select(volatile void *ptr);
static shim_clflush_func_t shim_clflush_func =  shim_clflush_select;

static inline void ALWAYS_INLINE shim_clflush_op(volatile void *ptr)
{
	asm volatile("clflush (%0)\n" : : "r"(ptr) : "memory");
}

static inline void ALWAYS_INLINE shim_clflush_nop(volatile void *ptr)
{
	(void)ptr;
}

static inline void ALWAYS_INLINE shim_clflush_select(volatile void *ptr)
{
	shim_clflush_func = stress_cpu_x86_has_clfsh() ? shim_clflush_op : shim_clflush_nop;

	shim_clflush_func(ptr);
}

/*
 *  shim_clflush()
 *	flush a cache line
 */
static inline void ALWAYS_INLINE shim_clflush(volatile void *ptr)
{
	shim_clflush_func(ptr);
}
#elif defined(DCACHE)
#define shim_clflush(ptr)	shim_cacheflush((char *)ptr, 64, DCACHE)
#else
#define shim_clflush(ptr)	do { } while (0) /* No-op */
#endif

#if !defined(HAVE_BUILTIN_PREFETCH) || defined(__PCC__)
/* a fake prefetch var-args no-op */
static inline void shim_builtin_prefetch(const void *addr, ...)
{
	va_list ap;

	va_start(ap, addr);
	va_end(ap);
}
#else
#define shim_builtin_prefetch		__builtin_prefetch
#endif

/*
 *  shim_mfence()
 *	serializing memory fence
 */
static inline void ALWAYS_INLINE shim_mfence(void)
{
#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_FENCE)
	 asm volatile ("fence" ::: "memory");
#else
#if NEED_GNUC(4, 2, 0) &&	\
    !defined(__PCC__)
	__sync_synchronize();
#else
#if defined(STRESS_ARCH_X86)
	asm volatile("mfence" : : : "memory");
#else
	/* Other arches not yet implemented for older GCC flavours */
#endif
#endif
#endif
}

#endif