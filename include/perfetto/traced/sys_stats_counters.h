/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_TRACED_SYS_STATS_COUNTERS_H_
#define INCLUDE_PERFETTO_TRACED_SYS_STATS_COUNTERS_H_

#include "perfetto/base/utils.h"
#include "perfetto/common/sys_stats_counters.pbzero.h"

#include <vector>

namespace perfetto {

struct KeyAndId {
  const char* str;
  int id;
};

constexpr KeyAndId kMeminfoKeys[] = {
    {"MemTotal", protos::pbzero::MeminfoCounters::MEM_TOTAL},
    {"MemFree", protos::pbzero::MeminfoCounters::MEM_FREE},
    {"MemAvailable", protos::pbzero::MeminfoCounters::MEM_AVAILABLE},
    {"Buffers", protos::pbzero::MeminfoCounters::BUFFERS},
    {"Cached", protos::pbzero::MeminfoCounters::CACHED},
    {"SwapCached", protos::pbzero::MeminfoCounters::SWAP_CACHED},
    {"Active", protos::pbzero::MeminfoCounters::ACTIVE},
    {"Inactive", protos::pbzero::MeminfoCounters::INACTIVE},
    {"Active(anon)", protos::pbzero::MeminfoCounters::ACTIVE_ANON},
    {"Inactive(anon)", protos::pbzero::MeminfoCounters::INACTIVE_ANON},
    {"Active(file)", protos::pbzero::MeminfoCounters::ACTIVE_FILE},
    {"Inactive(file)", protos::pbzero::MeminfoCounters::INACTIVE_FILE},
    {"Unevictable", protos::pbzero::MeminfoCounters::UNEVICTABLE},
    {"Mlocked", protos::pbzero::MeminfoCounters::MLOCKED},
    {"SwapTotal", protos::pbzero::MeminfoCounters::SWAP_TOTAL},
    {"SwapFree", protos::pbzero::MeminfoCounters::SWAP_FREE},
    {"Dirty", protos::pbzero::MeminfoCounters::DIRTY},
    {"Writeback", protos::pbzero::MeminfoCounters::WRITEBACK},
    {"AnonPages", protos::pbzero::MeminfoCounters::ANON_PAGES},
    {"Mapped", protos::pbzero::MeminfoCounters::MAPPED},
    {"Shmem", protos::pbzero::MeminfoCounters::SHMEM},
    {"Slab", protos::pbzero::MeminfoCounters::SLAB},
    {"SReclaimable", protos::pbzero::MeminfoCounters::SLAB_RECLAIMABLE},
    {"SUnreclaim", protos::pbzero::MeminfoCounters::SLAB_UNRECLAIMABLE},
    {"KernelStack", protos::pbzero::MeminfoCounters::KERNEL_STACK},
    {"PageTables", protos::pbzero::MeminfoCounters::PAGE_TABLES},
    {"CommitLimit", protos::pbzero::MeminfoCounters::COMMIT_LIMIT},
    {"Committed_AS", protos::pbzero::MeminfoCounters::COMMITED_AS},
    {"VmallocTotal", protos::pbzero::MeminfoCounters::VMALLOC_TOTAL},
    {"VmallocUsed", protos::pbzero::MeminfoCounters::VMALLOC_USED},
    {"VmallocChunk", protos::pbzero::MeminfoCounters::VMALLOC_CHUNK},
    {"CmaTotal", protos::pbzero::MeminfoCounters::CMA_TOTAL},
    {"CmaFree", protos::pbzero::MeminfoCounters::CMA_FREE},
};

const KeyAndId kVmstatKeys[] = {
    {"nr_free_pages", protos::pbzero::VmstatCounters::NR_FREE_PAGES},
    {"nr_alloc_batch", protos::pbzero::VmstatCounters::NR_ALLOC_BATCH},
    {"nr_inactive_anon", protos::pbzero::VmstatCounters::NR_INACTIVE_ANON},
    {"nr_active_anon", protos::pbzero::VmstatCounters::NR_ACTIVE_ANON},
    {"nr_inactive_file", protos::pbzero::VmstatCounters::NR_INACTIVE_FILE},
    {"nr_active_file", protos::pbzero::VmstatCounters::NR_ACTIVE_FILE},
    {"nr_unevictable", protos::pbzero::VmstatCounters::NR_UNEVICTABLE},
    {"nr_mlock", protos::pbzero::VmstatCounters::NR_MLOCK},
    {"nr_anon_pages", protos::pbzero::VmstatCounters::NR_ANON_PAGES},
    {"nr_mapped", protos::pbzero::VmstatCounters::NR_MAPPED},
    {"nr_file_pages", protos::pbzero::VmstatCounters::NR_FILE_PAGES},
    {"nr_dirty", protos::pbzero::VmstatCounters::NR_DIRTY},
    {"nr_writeback", protos::pbzero::VmstatCounters::NR_WRITEBACK},
    {"nr_slab_reclaimable",
     protos::pbzero::VmstatCounters::NR_SLAB_RECLAIMABLE},
    {"nr_slab_unreclaimable",
     protos::pbzero::VmstatCounters::NR_SLAB_UNRECLAIMABLE},
    {"nr_page_table_pages",
     protos::pbzero::VmstatCounters::NR_PAGE_TABLE_PAGES},
    {"nr_kernel_stack", protos::pbzero::VmstatCounters::NR_KERNEL_STACK},
    {"nr_overhead", protos::pbzero::VmstatCounters::NR_OVERHEAD},
    {"nr_unstable", protos::pbzero::VmstatCounters::NR_UNSTABLE},
    {"nr_bounce", protos::pbzero::VmstatCounters::NR_BOUNCE},
    {"nr_vmscan_write", protos::pbzero::VmstatCounters::NR_VMSCAN_WRITE},
    {"nr_vmscan_immediate_reclaim",
     protos::pbzero::VmstatCounters::NR_VMSCAN_IMMEDIATE_RECLAIM},
    {"nr_writeback_temp", protos::pbzero::VmstatCounters::NR_WRITEBACK_TEMP},
    {"nr_isolated_anon", protos::pbzero::VmstatCounters::NR_ISOLATED_ANON},
    {"nr_isolated_file", protos::pbzero::VmstatCounters::NR_ISOLATED_FILE},
    {"nr_shmem", protos::pbzero::VmstatCounters::NR_SHMEM},
    {"nr_dirtied", protos::pbzero::VmstatCounters::NR_DIRTIED},
    {"nr_written", protos::pbzero::VmstatCounters::NR_WRITTEN},
    {"nr_pages_scanned", protos::pbzero::VmstatCounters::NR_PAGES_SCANNED},
    {"workingset_refault", protos::pbzero::VmstatCounters::WORKINGSET_REFAULT},
    {"workingset_activate",
     protos::pbzero::VmstatCounters::WORKINGSET_ACTIVATE},
    {"workingset_nodereclaim",
     protos::pbzero::VmstatCounters::WORKINGSET_NODERECLAIM},
    {"nr_anon_transparent_hugepages",
     protos::pbzero::VmstatCounters::NR_ANON_TRANSPARENT_HUGEPAGES},
    {"nr_free_cma", protos::pbzero::VmstatCounters::NR_FREE_CMA},
    {"nr_swapcache", protos::pbzero::VmstatCounters::NR_SWAPCACHE},
    {"nr_dirty_threshold", protos::pbzero::VmstatCounters::NR_DIRTY_THRESHOLD},
    {"nr_dirty_background_threshold",
     protos::pbzero::VmstatCounters::NR_DIRTY_BACKGROUND_THRESHOLD},
    {"pgpgin", protos::pbzero::VmstatCounters::PGPGIN},
    {"pgpgout", protos::pbzero::VmstatCounters::PGPGOUT},
    {"pgpgoutclean", protos::pbzero::VmstatCounters::PGPGOUTCLEAN},
    {"pswpin", protos::pbzero::VmstatCounters::PSWPIN},
    {"pswpout", protos::pbzero::VmstatCounters::PSWPOUT},
    {"pgalloc_dma", protos::pbzero::VmstatCounters::PGALLOC_DMA},
    {"pgalloc_normal", protos::pbzero::VmstatCounters::PGALLOC_NORMAL},
    {"pgalloc_movable", protos::pbzero::VmstatCounters::PGALLOC_MOVABLE},
    {"pgfree", protos::pbzero::VmstatCounters::PGFREE},
    {"pgactivate", protos::pbzero::VmstatCounters::PGACTIVATE},
    {"pgdeactivate", protos::pbzero::VmstatCounters::PGDEACTIVATE},
    {"pgfault", protos::pbzero::VmstatCounters::PGFAULT},
    {"pgmajfault", protos::pbzero::VmstatCounters::PGMAJFAULT},
    {"pgrefill_dma", protos::pbzero::VmstatCounters::PGREFILL_DMA},
    {"pgrefill_normal", protos::pbzero::VmstatCounters::PGREFILL_NORMAL},
    {"pgrefill_movable", protos::pbzero::VmstatCounters::PGREFILL_MOVABLE},
    {"pgsteal_kswapd_dma", protos::pbzero::VmstatCounters::PGSTEAL_KSWAPD_DMA},
    {"pgsteal_kswapd_normal",
     protos::pbzero::VmstatCounters::PGSTEAL_KSWAPD_NORMAL},
    {"pgsteal_kswapd_movable",
     protos::pbzero::VmstatCounters::PGSTEAL_KSWAPD_MOVABLE},
    {"pgsteal_direct_dma", protos::pbzero::VmstatCounters::PGSTEAL_DIRECT_DMA},
    {"pgsteal_direct_normal",
     protos::pbzero::VmstatCounters::PGSTEAL_DIRECT_NORMAL},
    {"pgsteal_direct_movable",
     protos::pbzero::VmstatCounters::PGSTEAL_DIRECT_MOVABLE},
    {"pgscan_kswapd_dma", protos::pbzero::VmstatCounters::PGSCAN_KSWAPD_DMA},
    {"pgscan_kswapd_normal",
     protos::pbzero::VmstatCounters::PGSCAN_KSWAPD_NORMAL},
    {"pgscan_kswapd_movable",
     protos::pbzero::VmstatCounters::PGSCAN_KSWAPD_MOVABLE},
    {"pgscan_direct_dma", protos::pbzero::VmstatCounters::PGSCAN_DIRECT_DMA},
    {"pgscan_direct_normal",
     protos::pbzero::VmstatCounters::PGSCAN_DIRECT_NORMAL},
    {"pgscan_direct_movable",
     protos::pbzero::VmstatCounters::PGSCAN_DIRECT_MOVABLE},
    {"pgscan_direct_throttle",
     protos::pbzero::VmstatCounters::PGSCAN_DIRECT_THROTTLE},
    {"pginodesteal", protos::pbzero::VmstatCounters::PGINODESTEAL},
    {"slabs_scanned", protos::pbzero::VmstatCounters::SLABS_SCANNED},
    {"kswapd_inodesteal", protos::pbzero::VmstatCounters::KSWAPD_INODESTEAL},
    {"kswapd_low_wmark_hit_quickly",
     protos::pbzero::VmstatCounters::KSWAPD_LOW_WMARK_HIT_QUICKLY},
    {"kswapd_high_wmark_hit_quickly",
     protos::pbzero::VmstatCounters::KSWAPD_HIGH_WMARK_HIT_QUICKLY},
    {"pageoutrun", protos::pbzero::VmstatCounters::PAGEOUTRUN},
    {"allocstall", protos::pbzero::VmstatCounters::ALLOCSTALL},
    {"pgrotated", protos::pbzero::VmstatCounters::PGROTATED},
    {"drop_pagecache", protos::pbzero::VmstatCounters::DROP_PAGECACHE},
    {"drop_slab", protos::pbzero::VmstatCounters::DROP_SLAB},
    {"pgmigrate_success", protos::pbzero::VmstatCounters::PGMIGRATE_SUCCESS},
    {"pgmigrate_fail", protos::pbzero::VmstatCounters::PGMIGRATE_FAIL},
    {"compact_migrate_scanned",
     protos::pbzero::VmstatCounters::COMPACT_MIGRATE_SCANNED},
    {"compact_free_scanned",
     protos::pbzero::VmstatCounters::COMPACT_FREE_SCANNED},
    {"compact_isolated", protos::pbzero::VmstatCounters::COMPACT_ISOLATED},
    {"compact_stall", protos::pbzero::VmstatCounters::COMPACT_STALL},
    {"compact_fail", protos::pbzero::VmstatCounters::COMPACT_FAIL},
    {"compact_success", protos::pbzero::VmstatCounters::COMPACT_SUCCESS},
    {"compact_daemon_wake",
     protos::pbzero::VmstatCounters::COMPACT_DAEMON_WAKE},
    {"unevictable_pgs_culled",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_CULLED},
    {"unevictable_pgs_scanned",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_SCANNED},
    {"unevictable_pgs_rescued",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_RESCUED},
    {"unevictable_pgs_mlocked",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_MLOCKED},
    {"unevictable_pgs_munlocked",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_MUNLOCKED},
    {"unevictable_pgs_cleared",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_CLEARED},
    {"unevictable_pgs_stranded",
     protos::pbzero::VmstatCounters::UNEVICTABLE_PGS_STRANDED},
};

// Returns a lookup table of meminfo counter names addressable by counter id.
inline std::vector<const char*> BuildMeminfoCounterNames() {
  int max_id = 0;
  for (size_t i = 0; i < base::ArraySize(kMeminfoKeys); i++)
    max_id = std::max(max_id, kMeminfoKeys[i].id);
  std::vector<const char*> v;
  v.resize(static_cast<size_t>(max_id) + 1);
  for (size_t i = 0; i < base::ArraySize(kMeminfoKeys); i++)
    v[static_cast<size_t>(kMeminfoKeys[i].id)] = kMeminfoKeys[i].str;
  return v;
}

inline std::vector<const char*> BuildVmstatCounterNames() {
  int max_id = 0;
  for (size_t i = 0; i < base::ArraySize(kVmstatKeys); i++)
    max_id = std::max(max_id, kVmstatKeys[i].id);
  std::vector<const char*> v;
  v.resize(static_cast<size_t>(max_id) + 1);
  for (size_t i = 0; i < base::ArraySize(kVmstatKeys); i++)
    v[static_cast<size_t>(kVmstatKeys[i].id)] = kVmstatKeys[i].str;
  return v;
}

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACED_SYS_STATS_COUNTERS_H_
