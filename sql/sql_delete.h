/* Copyright (c) 2006, 2021, Oracle and/or its affiliates.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_DELETE_INCLUDED
#define SQL_DELETE_INCLUDED

#include <stddef.h>
#include <sys/types.h>

#include "my_base.h"  // ha_rows
#include "my_sqlcommand.h"
#include "my_table_map.h"
#include "sql/query_result.h"  // Query_result_interceptor
#include "sql/sql_cmd_dml.h"   // Sql_cmd_dml

class Item;
class Query_expression;
class Select_lex_visitor;
class THD;
class Unique;
struct TABLE;
struct TABLE_LIST;
template <class T>
class List;
template <typename T>
class SQL_I_List;

class Query_result_delete final : public Query_result_interceptor {
  /// Pointers to temporary files used for delayed deletion of rows
  Unique **tempfiles{nullptr};
  /// Pointers to table objects matching tempfiles
  TABLE **tables{nullptr};
  /// Number of tables being deleted from
  uint delete_table_count{0};
  /// Number of rows produced by the join
  ha_rows found_rows{0};
  /// Number of rows deleted
  ha_rows deleted_rows{0};
  /// Handler error status for the operation.
  int delete_error{0};
  /// Map of all tables to delete rows from
  table_map delete_table_map{0};
  /// Map of tables to delete from immediately
  table_map delete_immediate{0};
  // Map of transactional tables to be deleted from
  table_map transactional_table_map{0};
  /// Map of non-transactional tables to be deleted from
  table_map non_transactional_table_map{0};
  /// True if the full delete operation is complete
  bool delete_completed{false};
  /// True if some actual delete operation against non-transactional table done
  bool non_transactional_deleted{false};
  /*
     error handling (rollback and binlogging) can happen in send_eof()
     so that afterward send_error() needs to find out that.
  */
  bool error_handled{false};

 public:
  Query_result_delete() : Query_result_interceptor() {}
  bool need_explain_interceptor() const override { return true; }
  bool prepare(THD *thd, const mem_root_deque<Item *> &list,
               Query_expression *u) override;
  bool send_data(THD *thd, const mem_root_deque<Item *> &items) override;
  void send_error(THD *thd, uint errcode, const char *err) override;
  bool optimize() override;
  bool start_execution(THD *) override {
    delete_completed = false;
    return false;
  }
  int do_deletes(THD *thd);
  int do_table_deletes(THD *thd, TABLE *table);
  bool send_eof(THD *thd) override;
  inline ha_rows num_deleted() { return deleted_rows; }
  void abort_result_set(THD *thd) override;
  void cleanup(THD *thd) override;
  bool immediate_update(TABLE_LIST *t) const override;
};

class Sql_cmd_delete final : public Sql_cmd_dml {
 public:
  Sql_cmd_delete(bool multitable_arg, SQL_I_List<TABLE_LIST> *delete_tables_arg)
      : multitable(multitable_arg), delete_tables(delete_tables_arg),
	    returning_list(*THR_MALLOC) {}

  enum_sql_command sql_command_code() const override {
    return multitable ? SQLCOM_DELETE_MULTI : SQLCOM_DELETE;
  }

  bool is_single_table_plan() const override { return !multitable; }

  bool accept(THD *thd, Select_lex_visitor *visitor) override;

 protected:
  bool precheck(THD *thd) override;
  bool check_privileges(THD *thd) override;

  bool prepare_inner(THD *thd) override;

  bool execute_inner(THD *thd) override;

 private:
  bool delete_from_single_table(THD *thd);

  bool multitable;
  /**
    References to tables that are deleted from in a multitable delete statement.
    Only used to track such tables from the parser. In preparation and
    optimization, use the TABLE_LIST::updating property instead.
  */
  SQL_I_List<TABLE_LIST> *delete_tables;
 public:
  mem_root_deque<Item*> returning_list;
};

#endif /* SQL_DELETE_INCLUDED */
