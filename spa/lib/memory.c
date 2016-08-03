/* Simple Plugin API
 * Copyright (C) 2016 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "spa/memory.h"

#define MAX_POOLS       16
#define MAX_MEMORIES    1024

struct _SpaMemoryPool {
  bool valid;
  uint32_t id;
  SpaMemory memories[MAX_MEMORIES];
  unsigned int n_free;
  uint32_t free_mem[MAX_MEMORIES];
};

static SpaMemoryPool pools[MAX_POOLS];

static void
spa_memory_pool_init (SpaMemoryPool *pool, uint32_t id)
{
  int i;

  memset (pool, 0, sizeof (SpaMemoryPool));
  for (i = 0; i < MAX_MEMORIES; i++)
    pool->free_mem[i] = MAX_MEMORIES - 1 - i;
  pool->n_free = MAX_MEMORIES;
  pool->id = id;
  pool->valid = true;
}

void
spa_memory_init (void)
{
  static bool initialized = false;

  if (!initialized) {
    spa_memory_pool_init (&pools[0], 0);
    initialized = true;
  }
}

uint32_t
spa_memory_pool_get (uint32_t type)
{
  return pools[type].id;
}

uint32_t
spa_memory_pool_new (void)
{
  int i;

  for (i = 0; i < MAX_POOLS; i++) {
    if (!pools[i].valid) {
      spa_memory_pool_init (&pools[i], i);
      return i;
    }
  }
  return SPA_ID_INVALID;
}

void
spa_memory_pool_free (uint32_t pool_id)
{
  pools[pool_id].valid = false;
}

SpaMemory *
spa_memory_alloc (uint32_t pool_id)
{
  SpaMemory *mem;
  SpaMemoryPool *pool;
  uint32_t id;

  if (pool_id >= MAX_POOLS || !pools[pool_id].valid)
    return NULL;

  pool = &pools[pool_id];

  if (pool->n_free == 0)
    return NULL;

  id = pool->free_mem[pool->n_free - 1];
  pool->n_free--;

  mem = &pool->memories[id];
  mem->refcount = 1;
  mem->notify = NULL;
  mem->pool_id = pool_id;
  mem->id = id;

  return mem;
}

SpaMemory *
spa_memory_alloc_with_fd  (uint32_t pool_id, void *data, size_t size)
{
  SpaMemory *mem;
  char filename[] = "/dev/shm/spa-tmpfile.XXXXXX";

  if (!(mem = spa_memory_alloc (pool_id)))
    return NULL;

  mem->fd = mkostemp (filename, O_CLOEXEC);
  if (mem->fd == -1) {
    fprintf (stderr, "Failed to create temporary file: %s\n", strerror (errno));
    return NULL;
  }
  unlink (filename);

  if (data) {
    if (write (mem->fd, data, size) != (ssize_t) size) {
      fprintf (stderr, "Failed to write data: %s\n", strerror (errno));
      close (mem->fd);
      return NULL;
    }
  } else {
    if (ftruncate (mem->fd, size) < 0) {
      fprintf (stderr, "Failed to truncate temporary file: %s\n", strerror (errno));
      close (mem->fd);
      return NULL;
    }
  }

  mem->flags = SPA_MEMORY_FLAG_READWRITE;
  mem->ptr = NULL;
  mem->size = size;

  return mem;
}


SpaMemory *
spa_memory_import (uint32_t pool_id, uint32_t id)
{
  SpaMemory *mem = NULL;
  SpaMemoryPool *pool;
  int i;
  bool init = false;

  if (pool_id >= MAX_POOLS || !pools[pool_id].valid)
    return NULL;

  pool = &pools[pool_id];

  for (i = 0; i < pool->n_free; i++) {
    if (pool->free_mem[i] == id) {
      pool->free_mem[i] = pool->free_mem[pool->n_free - 1];
      pool->n_free--;
      init = true;
      break;
    }
  }

  mem = &pool->memories[id];
  if (init) {
    mem->refcount = 1;
    mem->notify = NULL;
    mem->pool_id = pool_id;
    mem->id = id;
  }

  return mem;
}

SpaResult
spa_memory_free (uint32_t pool_id, uint32_t id)
{
  SpaMemoryPool *pool;

  if (pool_id >= MAX_POOLS || !pools[pool_id].valid)
    return SPA_RESULT_INVALID_ARGUMENTS;

  pool = &pools[pool_id];
  pool->free_mem[pool->n_free] = id;
  pool->n_free++;

  return SPA_RESULT_OK;
}

SpaMemory *
spa_memory_find (uint32_t pool_id, uint32_t id)
{
  SpaMemoryPool *pool;

  if (pool_id >= MAX_POOLS || !pools[pool_id].valid)
    return NULL;

  pool = &pools[pool_id];

  if (id >= MAX_MEMORIES || pool->memories[id].refcount <= 0)
    return NULL;

  return &pool->memories[id];
}

void *
spa_memory_ensure_ptr (SpaMemory *mem)
{
  int prot = 0;

  if (mem == NULL)
    return NULL;

  if (mem->ptr)
    return mem->ptr;

  if (mem->flags & SPA_MEMORY_FLAG_READABLE)
    prot |= PROT_READ;
  if (mem->flags & SPA_MEMORY_FLAG_WRITABLE)
    prot |= PROT_WRITE;

  mem->ptr = mmap (NULL, mem->size, prot, MAP_SHARED, mem->fd, 0);
  if (mem->ptr == MAP_FAILED) {
    mem->ptr = NULL;
    fprintf (stderr, "Failed to mmap memory %p: %s\n", mem, strerror (errno));
  }
  return mem->ptr;
}