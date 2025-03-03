/* Copyright (c) 2011, 2015, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/**
  @file storage/perfschema/table_mems_by_account_by_event_name.cc
  Table MEMORY_SUMMARY_BY_ACCOUNT_BY_EVENT_NAME (implementation).
*/

#include "my_global.h"
#include "my_thread.h"
#include "pfs_instr_class.h"
#include "pfs_column_types.h"
#include "pfs_column_values.h"
#include "table_mems_by_account_by_event_name.h"
#include "pfs_global.h"
#include "pfs_visitor.h"
#include "pfs_memory.h"
#include "pfs_buffer_container.h"
#include "field.h"

THR_LOCK table_mems_by_account_by_event_name::m_table_lock;

PFS_engine_table_share
table_mems_by_account_by_event_name::m_share=
{
  { C_STRING_WITH_LEN("memory_summary_by_account_by_event_name") },
  &pfs_readonly_acl,
  table_mems_by_account_by_event_name::create,
  NULL, /* write_row */
  table_mems_by_account_by_event_name::delete_all_rows,
  table_mems_by_account_by_event_name::get_row_count,
  sizeof(pos_mems_by_account_by_event_name),
  &m_table_lock,
  { C_STRING_WITH_LEN("CREATE TABLE memory_summary_by_account_by_event_name("
  "USER CHAR(32) collate utf8_bin default null comment 'User portion of the account.',"
  "HOST CHAR(60) collate utf8_bin default null comment 'Host portion of the account.',"
  "EVENT_NAME VARCHAR(128) not null comment 'Event name.',"
  "COUNT_ALLOC BIGINT unsigned not null comment 'Total number of allocations to memory.',"
  "COUNT_FREE BIGINT unsigned not null comment 'Total number of attempts to free the allocated memory.',"
  "SUM_NUMBER_OF_BYTES_ALLOC BIGINT unsigned not null comment 'Total number of bytes allocated.',"
  "SUM_NUMBER_OF_BYTES_FREE BIGINT unsigned not null comment 'Total number of bytes freed',"
  "LOW_COUNT_USED BIGINT not null comment 'Lowest number of allocated blocks (lowest value of CURRENT_COUNT_USED).',"
  "CURRENT_COUNT_USED BIGINT not null comment 'Currently allocated blocks that have not been freed (COUNT_ALLOC minus COUNT_FREE).',"
  "HIGH_COUNT_USED BIGINT not null comment 'Highest number of allocated blocks (highest value of CURRENT_COUNT_USED).',"
  "LOW_NUMBER_OF_BYTES_USED BIGINT not null comment 'Lowest number of bytes used.',"
  "CURRENT_NUMBER_OF_BYTES_USED BIGINT not null comment 'Current number of bytes used (total allocated minus total freed).',"
  "HIGH_NUMBER_OF_BYTES_USED BIGINT not null comment 'Highest number of bytes used.')")},
  false  /* perpetual */
};

PFS_engine_table* table_mems_by_account_by_event_name::create(void)
{
  return new table_mems_by_account_by_event_name();
}

int
table_mems_by_account_by_event_name::delete_all_rows(void)
{
  reset_memory_by_thread();
  reset_memory_by_account();
  return 0;
}

ha_rows
table_mems_by_account_by_event_name::get_row_count(void)
{
  return global_account_container.get_row_count() * memory_class_max;
}

table_mems_by_account_by_event_name::table_mems_by_account_by_event_name()
  : PFS_engine_table(&m_share, &m_pos),
  m_row_exists(false), m_pos(), m_next_pos()
{}

void table_mems_by_account_by_event_name::reset_position(void)
{
  m_pos.reset();
  m_next_pos.reset();
}

int table_mems_by_account_by_event_name::rnd_next(void)
{
  PFS_account *account;
  PFS_memory_class *memory_class;
  bool has_more_account= true;

  for (m_pos.set_at(&m_next_pos);
       has_more_account;
       m_pos.next_account())
  {
    account= global_account_container.get(m_pos.m_index_1, & has_more_account);
    if (account != NULL)
    {
      do
      {
        memory_class= find_memory_class(m_pos.m_index_2);
        if (memory_class != NULL)
        {
          if (! memory_class->is_global())
          {
            make_row(account, memory_class);
            m_next_pos.set_after(&m_pos);
            return 0;
          }

          m_pos.next_class();
        }
      }
      while (memory_class != NULL);
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_mems_by_account_by_event_name::rnd_pos(const void *pos)
{
  PFS_account *account;
  PFS_memory_class *memory_class;

  set_position(pos);

  account= global_account_container.get(m_pos.m_index_1);
  if (account != NULL)
  {
    memory_class= find_memory_class(m_pos.m_index_2);
    if (memory_class != NULL)
    {
      if (! memory_class->is_global())
      {
        make_row(account, memory_class);
        return 0;
      }
    }
  }

  return HA_ERR_RECORD_DELETED;
}

void table_mems_by_account_by_event_name
::make_row(PFS_account *account, PFS_memory_class *klass)
{
  pfs_optimistic_state lock;
  m_row_exists= false;

  account->m_lock.begin_optimistic_lock(&lock);

  if (m_row.m_account.make_row(account))
    return;

  m_row.m_event_name.make_row(klass);

  PFS_connection_memory_visitor visitor(klass);
  PFS_connection_iterator::visit_account(account,
                                         true,  /* threads */
                                         false, /* THDs */
                                         & visitor);

  if (! account->m_lock.end_optimistic_lock(&lock))
    return;

  m_row_exists= true;
  m_row.m_stat.set(& visitor.m_stat);
}

int table_mems_by_account_by_event_name::read_row_values(TABLE *table,
                                                    unsigned char *buf,
                                                    Field **fields,
                                                    bool read_all)
{
  Field *f;

  if (unlikely(! m_row_exists))
    return HA_ERR_RECORD_DELETED;

  /* Set the null bits */
  DBUG_ASSERT(table->s->null_bytes == 1);
  buf[0]= 0;

  for (; (f= *fields) ; fields++)
  {
    if (read_all || bitmap_is_set(table->read_set, f->field_index))
    {
      switch(f->field_index)
      {
      case 0: /* USER */
      case 1: /* HOST */
        m_row.m_account.set_field(f->field_index, f);
        break;
      case 2: /* EVENT_NAME */
        m_row.m_event_name.set_field(f);
        break;
      default: /* 3, ... HIGH_NUMBER_OF_BYTES_USED */
        m_row.m_stat.set_field(f->field_index - 3, f);
        break;
      }
    }
  }

  return 0;
}

