#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t PMEM_RAM_BASE = 0x80000000u;
constexpr uint32_t RAM_SIZE = 64u * 1024u * 1024u;
constexpr uint32_t PHYSICAL_MEMORY_LENGTH = RAM_SIZE / sizeof(uint32_t);

extern uint32_t *p_memory;

bool pmem_init();
void pmem_release();
void pmem_clear_all();

bool pmem_is_ram_addr(uint32_t paddr, uint32_t size = 4u);
uint32_t pmem_read(uint32_t paddr);
void pmem_write(uint32_t paddr, uint32_t data);

void pmem_memcpy_to_ram(uint32_t ram_paddr, const void *src, size_t len);
void pmem_memcpy_from_ram(void *dst, uint32_t ram_paddr, size_t len);

uint32_t *pmem_ram_ptr();
