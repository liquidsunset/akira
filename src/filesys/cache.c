#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>
#include <debug.h>

#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "lib/kernel/bitmap.h"
#include "threads/thread.h"
#include "threads/malloc.h"

struct cache_entry cache_data[CACHE_SIZE];
struct bitmap * cache_bitmap;
struct lock cache_lock;
void * cache;

static void evict_cache_entry(size_t i);
static int next_free_entry(void);
static int cache_entry_by_sector(block_sector_t sector);

void
cache_init(void)
{
	cache_bitmap = bitmap_create(CACHE_SIZE);
	if(cache_bitmap == NULL)
		PANIC("Unable to create cache bitmap");
	lock_init(&cache_lock);

	int i;
	for(i = 0; i < CACHE_SIZE; i++)
	{
		lock_init(&cache_data[i].l);
	}

	cache = malloc(CACHE_SIZE * BLOCK_SECTOR_SIZE);
	if(cache == NULL)
		PANIC("Unable to allocate buffer cache");
}

void
cache_read(block_sector_t sector, void *buffer)
{
	lock_acquire(&cache_lock);
	int cache_index = cache_entry_by_sector(sector);
	if(cache_index == -1)
	{
		cache_index = next_free_entry();
		bitmap_set(cache_bitmap, cache_index, true);
		cache_data[cache_index].sector = sector;
		block_read(fs_device, sector, cache + cache_index * BLOCK_SECTOR_SIZE);
	}
	// printf("Adding cache entry for %d at %d\n", sector, cache_index);

	cache_data[cache_index].accessed = true;
	memcpy(buffer, cache + cache_index * BLOCK_SECTOR_SIZE, BLOCK_SECTOR_SIZE);
	lock_release(&cache_lock);
}

void
cache_write(block_sector_t sector, const void *buffer)
{
	lock_acquire(&cache_lock);
	int cache_index = cache_entry_by_sector(sector);
	if(cache_index == -1)
	{
		cache_index = next_free_entry();
		bitmap_set(cache_bitmap, cache_index, true);
		cache_data[cache_index].sector = sector;
	}

	cache_data[cache_index].accessed = true;
	cache_data[cache_index].dirty = true;

	memcpy(cache + cache_index * BLOCK_SECTOR_SIZE, buffer, BLOCK_SECTOR_SIZE);
	lock_release(&cache_lock);
}

static int
cache_entry_by_sector(block_sector_t sector)
{
	int i;
	for(i = 0; i < CACHE_SIZE; i++)
	{
		if(bitmap_test(cache_bitmap, i) && cache_data[i].sector == sector)
			return i;
	}

	return -1;
}

static void
evict_cache_entry(size_t i)
{
	ASSERT(i < CACHE_SIZE);
	ASSERT(cache_data[i].sector < block_size(fs_device));

	if(cache_data[i].dirty)
		block_write(fs_device, cache_data[i].sector, cache + (i * BLOCK_SECTOR_SIZE));
}

static int
next_free_entry(void)
{
	static int clock_index = 0;
	int i;
	size_t free_slots;

	free_slots = bitmap_count(cache_bitmap, 0, CACHE_SIZE, false);
	if(free_slots == 0)
	{
		for(i = 0; i < CACHE_SIZE + 1; i++)
		{
			int index = (clock_index + i) % CACHE_SIZE;
			if(cache_data[index].accessed)
			{
				cache_data[index].accessed = false;
			}
			else
			{
				clock_index = (index + 1) % CACHE_SIZE;
				evict_cache_entry(index);
				bitmap_set(cache_bitmap, index, false);
			}
		}
	}

	for(i = 0; i < CACHE_SIZE; i++)
	{
		if(!bitmap_test(cache_bitmap, i))
		{
			return i;
		}
	}

	PANIC("Buffer cache is full and we are not able to evict");
}