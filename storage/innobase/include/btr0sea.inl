/*****************************************************************************

Copyright (c) 1996, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2020, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/********************************************************************//**
@file include/btr0sea.ic
The index tree adaptive search

Created 2/17/1996 Heikki Tuuri
*************************************************************************/

#include "dict0mem.h"
#include "btr0cur.h"
#include "buf0buf.h"

/** Create and initialize search info.
@param[in,out]	heap		heap where created
@return own: search info struct */
UNIV_INLINE
btr_search_t*
btr_search_info_create(mem_heap_t* heap)
{
	btr_search_t*	info = static_cast<btr_search_t*>(
		mem_heap_zalloc(heap, sizeof(btr_search_t)));
	ut_d(info->magic_n = BTR_SEARCH_MAGIC_N);
#ifdef BTR_CUR_HASH_ADAPT
	info->n_fields = 1;
	info->left_side = TRUE;
#endif /* BTR_CUR_HASH_ADAPT */
	return(info);
}

#ifdef BTR_CUR_HASH_ADAPT
/*********************************************************************//**
Updates the search info. */
void
btr_search_info_update_slow(
/*========================*/
	btr_search_t*	info,	/*!< in/out: search info */
	btr_cur_t*	cursor);/*!< in: cursor which was just positioned */

/*********************************************************************//**
Updates the search info. */
UNIV_INLINE
void
btr_search_info_update(
/*===================*/
	dict_index_t*	index,	/*!< in: index of the cursor */
	btr_cur_t*	cursor)	/*!< in: cursor which was just positioned */
{
	ut_ad(!rw_lock_own_flagged(btr_get_search_latch(index),
				   RW_LOCK_FLAG_X | RW_LOCK_FLAG_S));

	if (dict_index_is_spatial(index) || !btr_search_enabled) {
		return;
	}

	btr_search_t*	info;
	info = btr_search_get_info(index);

	info->hash_analysis++;

	if (info->hash_analysis < BTR_SEARCH_HASH_ANALYSIS) {

		/* Do nothing */

		return;

	}

	ut_ad(cursor->flag != BTR_CUR_HASH);

	btr_search_info_update_slow(info, cursor);
}

/** Lock all search latches in exclusive mode. */
UNIV_INLINE
void
btr_search_x_lock_all()
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		rw_lock_x_lock(btr_search_latches[i]);
	}
}

/** Unlock all search latches from exclusive mode. */
UNIV_INLINE
void
btr_search_x_unlock_all()
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		rw_lock_x_unlock(btr_search_latches[i]);
	}
}

/** Lock all search latches in shared mode. */
UNIV_INLINE
void
btr_search_s_lock_all()
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		rw_lock_s_lock(btr_search_latches[i]);
	}
}

/** Unlock all search latches from shared mode. */
UNIV_INLINE
void
btr_search_s_unlock_all()
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		rw_lock_s_unlock(btr_search_latches[i]);
	}
}

#ifdef UNIV_DEBUG
/** Check if thread owns all the search latches.
@param[in]	mode	lock mode check
@retval true if owns all of them
@retval false if does not own some of them */
UNIV_INLINE
bool
btr_search_own_all(ulint mode)
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		if (!rw_lock_own(btr_search_latches[i], mode)) {
			return(false);
		}
	}
	return(true);
}

/** Check if thread owns any of the search latches.
@param[in]	mode	lock mode check
@retval true if owns any of them
@retval false if owns no search latch */
UNIV_INLINE
bool
btr_search_own_any(ulint mode)
{
	for (ulint i = 0; i < btr_ahi_parts; ++i) {
		if (rw_lock_own(btr_search_latches[i], mode)) {
			return(true);
		}
	}
	return(false);
}
#endif /* UNIV_DEBUG */

/** Get the adaptive hash search index latch for a b-tree.
@param[in]	index	b-tree index
@return latch */
UNIV_INLINE
rw_lock_t*
btr_get_search_latch(const dict_index_t* index)
{
	ut_ad(index != NULL);

	ulint	ifold = ut_fold_ulint_pair(static_cast<ulint>(index->id),
					   static_cast<ulint>(index->space));

	return(btr_search_latches[ifold % btr_ahi_parts]);
}

/** Get the hash-table based on index attributes.
A table is selected from an array of tables using pair of index-id, space-id.
@param[in]	index	index handler
@return hash table */
UNIV_INLINE
hash_table_t*
btr_get_search_table(const dict_index_t* index)
{
	ut_ad(index != NULL);

	ulint	ifold = ut_fold_ulint_pair(static_cast<ulint>(index->id),
					   static_cast<ulint>(index->space));

	return(btr_search_sys->hash_tables[ifold % btr_ahi_parts]);
}
#endif /* BTR_CUR_HASH_ADAPT */