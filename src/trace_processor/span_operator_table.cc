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

#include "src/trace_processor/span_operator_table.h"

#include <sqlite3.h>
#include <string.h>
#include <algorithm>
#include <set>

#include "perfetto/base/logging.h"
#include "perfetto/base/string_splitter.h"
#include "perfetto/base/string_view.h"
#include "src/trace_processor/query_utils.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

constexpr int64_t kI64Max = std::numeric_limits<int64_t>::max();
constexpr uint64_t kU64Max = std::numeric_limits<uint64_t>::max();

}  // namespace

SpanOperatorTable::SpanOperatorTable(sqlite3* db, const TraceStorage*)
    : db_(db) {}

void SpanOperatorTable::RegisterTable(sqlite3* db,
                                      const TraceStorage* storage) {
  Table::Register<SpanOperatorTable>(db, storage, "span");
}

Table::Schema SpanOperatorTable::CreateSchema(int argc,
                                              const char* const* argv) {
  // argv[0] - argv[2] are SQLite populated fields which are always present.
  if (argc < 5) {
    PERFETTO_ELOG("SPAN JOIN expected at least 2 args, received %d", argc - 3);
    return Table::Schema({}, {});
  }

  std::string t1_raw_desc = reinterpret_cast<const char*>(argv[3]);
  auto t1_desc = TableDescriptor::Parse(t1_raw_desc);

  std::string t2_raw_desc = reinterpret_cast<const char*>(argv[4]);
  auto t2_desc = TableDescriptor::Parse(t2_raw_desc);

  // TODO(lalitm): add logic to ensure that the tables that are being joined
  // are actually valid to be joined i.e. they have the ts and dur columns and
  // have the join column.
  auto t1_cols = query_utils::GetColumnsForTable(db_, t1_desc.name);
  auto t2_cols = query_utils::GetColumnsForTable(db_, t2_desc.name);

  t1_defn_ = TableDefinition(t1_desc.name, t1_desc.partition_col, t1_cols);
  t2_defn_ = TableDefinition(t2_desc.name, t2_desc.partition_col, t2_cols);

  std::vector<Table::Column> cols;
  cols.emplace_back(Column::kTimestamp, "ts", ColumnType::kUlong);
  cols.emplace_back(Column::kDuration, "dur", ColumnType::kUlong);

  bool is_same_partition = t1_desc.partition_col == t2_desc.partition_col;
  const auto& partition_col = t1_desc.partition_col;
  if (is_same_partition)
    cols.emplace_back(Column::kPartition, partition_col, ColumnType::kLong);

  CreateSchemaColsForDefn(t1_defn_, is_same_partition, &cols);
  CreateSchemaColsForDefn(t2_defn_, is_same_partition, &cols);

  std::vector<size_t> primary_keys;
  if (is_same_partition)
    primary_keys = {Column::kPartition, Column::kTimestamp};
  else
    primary_keys = {Column::kTimestamp};
  return Schema(cols, primary_keys);
}

void SpanOperatorTable::CreateSchemaColsForDefn(
    const TableDefinition& defn,
    bool is_same_partition,
    std::vector<Table::Column>* cols) {
  for (size_t i = 0; i < defn.columns().size(); i++) {
    const auto& n = defn.columns()[i].name();

    ColumnLocator* locator = &global_index_to_column_locator_[cols->size()];
    locator->defn = &defn;
    locator->col_index = i;

    if (n == "ts" || n == "dur" ||
        (n == defn.partition_col() && is_same_partition)) {
      continue;
    }
    cols->emplace_back(cols->size(), n, defn.columns()[i].type());
  }
}

std::unique_ptr<Table::Cursor> SpanOperatorTable::CreateCursor(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  // Currently we need at least one table partitioned.
  PERFETTO_CHECK(t1_defn_.is_parititioned() || t2_defn_.is_parititioned());

  if (t1_defn_.is_parititioned() && t2_defn_.is_parititioned()) {
    // If both columns are partitioned, we need both to be partitioned by
    // the same column.
    PERFETTO_CHECK(t1_defn_.partition_col() == t2_defn_.partition_col());

    auto cursor = std::unique_ptr<SpanOperatorTable::SamePartitionCursor>(
        new SpanOperatorTable::SamePartitionCursor(this, db_));
    int value = cursor->Initialize(qc, argv);
    return value != SQLITE_OK ? nullptr : std::move(cursor);
  }

  auto* partitioned = t1_defn_.is_parititioned() ? &t1_defn_ : &t2_defn_;
  auto* unpartitioned = t1_defn_.is_parititioned() ? &t2_defn_ : &t1_defn_;

  bool sparse =
      query_utils::IsCountOfTableBelow(db_, unpartitioned->name(), 1000);
  if (sparse) {
    auto cursor =
        std::unique_ptr<SpanOperatorTable::SparseSinglePartitionCursor>(
            new SpanOperatorTable::SparseSinglePartitionCursor(
                this, db_, partitioned, unpartitioned));
    int value = cursor->Initialize(qc, argv);
    return value != SQLITE_OK ? nullptr : std::move(cursor);
  }

  auto cursor = std::unique_ptr<SpanOperatorTable::DenseSinglePartitionCursor>(
      new SpanOperatorTable::DenseSinglePartitionCursor(this, db_, partitioned,
                                                        unpartitioned));
  int value = cursor->Initialize(qc, argv);
  return value != SQLITE_OK ? nullptr : std::move(cursor);
}

int SpanOperatorTable::BestIndex(const QueryConstraints&, BestIndexInfo*) {
  // TODO(lalitm): figure out cost estimation.
  return SQLITE_OK;
}

std::vector<std::string> SpanOperatorTable::ComputeSqlConstraintsForDefinition(
    const TableDefinition& defn,
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  std::vector<std::string> constraints;
  for (size_t i = 0; i < qc.constraints().size(); i++) {
    const auto& cs = qc.constraints()[i];
    if (cs.iColumn == Column::kTimestamp || cs.iColumn == Column::kTimestamp) {
      // We don't support constraints on ts or duration in the child tables.
      PERFETTO_DCHECK(false);
      continue;
    }

    size_t col_idx = static_cast<size_t>(cs.iColumn);
    const auto& locator = global_index_to_column_locator_[col_idx];
    if (locator.defn != &defn)
      continue;

    const auto& col_name = defn.columns()[locator.col_index].name();
    auto op = sqlite_utils::OpToString(cs.op);
    auto value = sqlite_utils::SqliteValueAsString(argv[i]);

    constraints.emplace_back("`" + col_name + "`" + op + value);
  }
  return constraints;
}

bool SpanOperatorTable::IsOverlappingSpan(
    TableQueryState* t1,
    TableQueryState* t2,
    TableQueryState** next_stepped_table) {
  if (t1->ts_end() <= t2->ts_start() || t1->ts_start() == t1->ts_end()) {
    *next_stepped_table = t1;
    return false;
  } else if (t2->ts_end() <= t1->ts_start() || t2->ts_start() == t2->ts_end()) {
    *next_stepped_table = t2;
    return false;
  }
  *next_stepped_table = t1->ts_end() <= t2->ts_end() ? t1 : t2;
  return true;
}

SpanOperatorTable::TableQueryState::TableQueryState(
    SpanOperatorTable* table,
    const TableDefinition* definition,
    sqlite3* db)
    : defn_(definition), db_(db), table_(table) {
  ts_start_col_index_ = defn_->ColumnIndexByName("ts");
  dur_col_index_ = defn_->ColumnIndexByName("dur");
  if (defn_->is_parititioned())
    partition_col_index_ = defn_->ColumnIndexByName(defn_->partition_col());
}

int SpanOperatorTable::TableQueryState::Step() {
  sqlite3_stmt* stmt = stmt_.get();
  int res = sqlite3_step(stmt);
  if (res == SQLITE_ROW) {
    int64_t ts = sqlite3_column_int64(stmt, ts_start_col_index_);
    int64_t dur = sqlite3_column_int64(stmt, dur_col_index_);
    ts_start_ = static_cast<uint64_t>(ts);
    ts_end_ = ts_start_ + static_cast<uint64_t>(dur);

    if (defn_->is_parititioned())
      partition_ = sqlite3_column_int64(stmt, partition_col_index_);
  } else if (res == SQLITE_DONE) {
    ts_start_ = kU64Max;
    ts_end_ = kU64Max;

    if (defn_->is_parititioned())
      partition_ = kI64Max;
  }
  return res;
}

std::vector<std::string> SpanOperatorTable::TableQueryState::ComputeConstraints(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  return table_->ComputeSqlConstraintsForDefinition(*defn_, qc, argv);
}

std::string SpanOperatorTable::TableQueryState::CreateSqlQuery(
    const std::vector<std::string>& cs,
    bool order_by_partition) {
  // TODO(lalitm): pass through constraints on other tables to those tables.
  std::string sql;
  sql += "SELECT";
  for (size_t i = 0; i < defn_->columns().size(); i++) {
    const auto& col = defn_->columns()[i];
    if (i != 0)
      sql += ",";
    sql += " " + col.name();
  }
  sql += " FROM " + defn_->name();
  sql += " WHERE 1";
  for (const auto& c : cs) {
    sql += " AND " + c;
  }
  sql += " ORDER BY";

  // Check that if we are ordering by partition, we are actually partitioned.
  PERFETTO_DCHECK(!order_by_partition || defn_->is_parititioned());
  if (order_by_partition)
    sql += " `" + defn_->partition_col() + "`,";
  sql += " ts;";
  return sql;
}

int SpanOperatorTable::TableQueryState::PrepareRawStmt(const std::string& sql) {
  PERFETTO_DLOG("%s", sql.c_str());
  int size = static_cast<int>(sql.size());

  sqlite3_stmt* stmt = nullptr;
  int err = sqlite3_prepare_v2(db_, sql.c_str(), size, &stmt, nullptr);
  stmt_.reset(stmt);
  return err;
}

void SpanOperatorTable::TableQueryState::ReportSqliteResult(
    sqlite3_context* context,
    size_t index) {
  sqlite3_stmt* stmt = stmt_.get();
  int idx = static_cast<int>(index);
  switch (sqlite3_column_type(stmt, idx)) {
    case SQLITE_INTEGER:
      sqlite3_result_int64(context, sqlite3_column_int64(stmt, idx));
      break;
    case SQLITE_FLOAT:
      sqlite3_result_double(context, sqlite3_column_double(stmt, idx));
      break;
    case SQLITE_TEXT: {
      // TODO(lalitm): note for future optimizations: if we knew the addresses
      // of the string intern pool, we could check if the string returned here
      // comes from the pool, and pass it as non-transient.
      const auto kSqliteTransient =
          reinterpret_cast<sqlite3_destructor_type>(-1);
      auto ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
      sqlite3_result_text(context, ptr, -1, kSqliteTransient);
      break;
    }
  }
}

SpanOperatorTable::SparseSinglePartitionCursor::SparseSinglePartitionCursor(
    SpanOperatorTable* table,
    sqlite3* db,
    const TableDefinition* partitioned,
    const TableDefinition* unpartitioned)
    : partitioned_(table, partitioned, db),
      unpartitioned_(table, unpartitioned, db),
      table_(table) {}

int SpanOperatorTable::SparseSinglePartitionCursor::Initialize(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  auto cs = unpartitioned_.ComputeConstraints(qc, argv);
  auto sql = unpartitioned_.CreateSqlQuery(cs, false);
  int err = unpartitioned_.PrepareRawStmt(sql);
  if (err != SQLITE_OK)
    return err;

  err = unpartitioned_.Step();
  if (err == SQLITE_DONE)
    return SQLITE_OK;
  else if (err != SQLITE_ROW)
    return err;

  partitioned_constraints_ = partitioned_.ComputeConstraints(qc, argv);
  err = UpdatePartitionedQuery();
  if (err != SQLITE_OK)
    return err;

  return Next();
}

SpanOperatorTable::SparseSinglePartitionCursor::~SparseSinglePartitionCursor() {
}

int SpanOperatorTable::SparseSinglePartitionCursor::UpdatePartitionedQuery() {
  auto constraints = partitioned_constraints_;
  constraints.push_back("ts_end>=" + std::to_string(unpartitioned_.ts_start()));
  constraints.push_back("ts<=" + std::to_string(unpartitioned_.ts_end()));

  auto sql = partitioned_.CreateSqlQuery(constraints, false);
  int err = partitioned_.PrepareRawStmt(sql);
  if (err != SQLITE_OK)
    return err;
  return SQLITE_OK;
}

int SpanOperatorTable::SparseSinglePartitionCursor::Next() {
  while (unpartitioned_.ts_start() < kU64Max) {
    int err = partitioned_.Step();
    if (err == SQLITE_ROW)
      break;
    else if (err != SQLITE_DONE)
      return err;

    err = unpartitioned_.Step();
    if (err == SQLITE_DONE)
      break;
    else if (err != SQLITE_ROW)
      return err;
    UpdatePartitionedQuery();
  }
  return SQLITE_OK;
}

int SpanOperatorTable::SparseSinglePartitionCursor::Eof() {
  return unpartitioned_.ts_start() == kU64Max;
}

int SpanOperatorTable::SparseSinglePartitionCursor::Column(
    sqlite3_context* context,
    int N) {
  switch (N) {
    case Column::kTimestamp: {
      auto max_ts =
          std::max(partitioned_.ts_start(), unpartitioned_.ts_start());
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(max_ts));
      break;
    }
    case Column::kDuration: {
      auto max_start =
          std::max(partitioned_.ts_start(), unpartitioned_.ts_start());
      auto min_end = std::min(partitioned_.ts_end(), unpartitioned_.ts_end());
      PERFETTO_DCHECK(min_end > max_start);

      auto dur = min_end - max_start;
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(dur));
      break;
    }
    default: {
      size_t index = static_cast<size_t>(N);
      const auto& locator = table_->global_index_to_column_locator_[index];
      if (locator.defn == partitioned_.definition())
        partitioned_.ReportSqliteResult(context, locator.col_index);
      else
        unpartitioned_.ReportSqliteResult(context, locator.col_index);
      break;
    }
  }
  return SQLITE_OK;
}

SpanOperatorTable::DenseSinglePartitionCursor::DenseSinglePartitionCursor(
    SpanOperatorTable* table,
    sqlite3* db,
    const TableDefinition* partitioned,
    const TableDefinition* unpartitioned)
    : partitioned_(table, partitioned, db),
      unpartitioned_(table, unpartitioned, db),
      table_(table) {}

int SpanOperatorTable::DenseSinglePartitionCursor::Initialize(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  auto cs = partitioned_.ComputeConstraints(qc, argv);
  int err = partitioned_.PrepareRawStmt(partitioned_.CreateSqlQuery(cs, true));
  if (err != SQLITE_OK)
    return err;

  err = partitioned_.Step();
  if (err == SQLITE_DONE)
    return SQLITE_OK;
  else if (err != SQLITE_ROW)
    return err;

  cs = unpartitioned_.ComputeConstraints(qc, argv);
  unpartitioned_sql_ = unpartitioned_.CreateSqlQuery(cs, false);
  err = UpdateUnpartitionedQuery();
  if (err != SQLITE_OK)
    return err;

  next_stepped_table_ = &unpartitioned_;
  return Next();
}

SpanOperatorTable::DenseSinglePartitionCursor::~DenseSinglePartitionCursor() {}

int SpanOperatorTable::DenseSinglePartitionCursor::Next() {
  while (partitioned_.ts_start() < kU64Max) {
    int64_t old_partition = partitioned_.partition();

    int err = next_stepped_table_->Step();
    for (; err == SQLITE_ROW; err = next_stepped_table_->Step()) {
      // If we've gone to the the next partition, we need to reset the
      // unpartitioned table.
      if (partitioned_.partition() != old_partition)
        break;

      if (IsOverlappingSpan(&unpartitioned_, &partitioned_,
                            &next_stepped_table_))
        return SQLITE_OK;
    }

    // If there's a real error code, return it.
    if (err != SQLITE_ROW && err != SQLITE_DONE)
      return err;

    // Either one of the tables finished or we moved to the next partition.

    // Case 1: the partitioned table finished. This means there are no more
    // spans to return.
    if (partitioned_.ts_start() == kU64Max)
      return SQLITE_OK;

    // Case 2: the partitioned table has moved to the next partition. This means
    // we need to reset the unpartitioned table and step it.
    if (partitioned_.partition() != old_partition) {
      err = UpdateUnpartitionedQuery();
      if (err != SQLITE_OK)
        return err;
      next_stepped_table_ = &unpartitioned_;
      continue;
    }

    // Case 3: the unpartitioned table finished. This means we need to forward
    // the partitioned table until we hit a new partition.
    PERFETTO_DCHECK(unpartitioned_.ts_start() == kU64Max);
    while (old_partition == partitioned_.partition()) {
      err = partitioned_.Step();
      if (err == SQLITE_DONE)
        return SQLITE_OK;
      else if (err != SQLITE_ROW)
        return err;
    }
  }
  return SQLITE_OK;
}

int SpanOperatorTable::DenseSinglePartitionCursor::UpdateUnpartitionedQuery() {
  return unpartitioned_.PrepareRawStmt(unpartitioned_sql_);
}

int SpanOperatorTable::DenseSinglePartitionCursor::Eof() {
  return partitioned_.ts_start() == kU64Max;
}

int SpanOperatorTable::DenseSinglePartitionCursor::Column(
    sqlite3_context* context,
    int N) {
  switch (N) {
    case Column::kTimestamp: {
      auto max_ts =
          std::max(partitioned_.ts_start(), unpartitioned_.ts_start());
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(max_ts));
      break;
    }
    case Column::kDuration: {
      auto max_start =
          std::max(partitioned_.ts_start(), unpartitioned_.ts_start());
      auto min_end = std::min(partitioned_.ts_end(), unpartitioned_.ts_end());
      PERFETTO_DCHECK(min_end > max_start);

      auto dur = min_end - max_start;
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(dur));
      break;
    }
    default: {
      size_t index = static_cast<size_t>(N);
      const auto& locator = table_->global_index_to_column_locator_[index];
      if (locator.defn == partitioned_.definition())
        partitioned_.ReportSqliteResult(context, locator.col_index);
      else
        unpartitioned_.ReportSqliteResult(context, locator.col_index);
      break;
    }
  }
  return SQLITE_OK;
}

SpanOperatorTable::SamePartitionCursor::SamePartitionCursor(
    SpanOperatorTable* table,
    sqlite3* db)
    : t1_(table, &table->t1_defn_, db),
      t2_(table, &table->t2_defn_, db),
      table_(table) {}

int SpanOperatorTable::SamePartitionCursor::Initialize(
    const QueryConstraints& qc,
    sqlite3_value** argv) {
  auto cs = t1_.ComputeConstraints(qc, argv);
  int err = t1_.PrepareRawStmt(t1_.CreateSqlQuery(cs, true));
  if (err != SQLITE_OK)
    return err;

  cs = t2_.ComputeConstraints(qc, argv);
  err = t2_.PrepareRawStmt(t2_.CreateSqlQuery(cs, true));
  if (err != SQLITE_OK)
    return err;

  // We step table 2 and allow Next() to step from table 1.
  next_stepped_table_ = &t1_;
  err = t2_.Step();

  // If there's no data in this table, then we are done without even looking
  // at the other table.
  if (err != SQLITE_ROW)
    return err == SQLITE_DONE ? SQLITE_OK : err;

  // Otherwise, find an overlapping span.
  return Next();
}

SpanOperatorTable::SamePartitionCursor::~SamePartitionCursor() {}

int SpanOperatorTable::SamePartitionCursor::Next() {
  int err = next_stepped_table_->Step();
  for (; err == SQLITE_ROW; err = next_stepped_table_->Step()) {
    // Get both tables on the same join value.
    if (t1_.partition() < t2_.partition()) {
      next_stepped_table_ = &t1_;
      continue;
    } else if (t2_.partition() < t1_.partition()) {
      next_stepped_table_ = &t2_;
      continue;
    }
    bool overlap = IsOverlappingSpan(&t1_, &t2_, &next_stepped_table_);
    if (overlap)
      return SQLITE_OK;
  }
  return err == SQLITE_DONE ? SQLITE_OK : err;
}

int SpanOperatorTable::SamePartitionCursor::Eof() {
  return t1_.ts_start() == kU64Max || t2_.ts_start() == kU64Max;
}

int SpanOperatorTable::SamePartitionCursor::Column(sqlite3_context* context,
                                                   int N) {
  switch (N) {
    case Column::kTimestamp: {
      auto max_ts = std::max(t1_.ts_start(), t2_.ts_start());
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(max_ts));
      break;
    }
    case Column::kDuration: {
      auto max_start = std::max(t1_.ts_start(), t2_.ts_start());
      auto min_end = std::min(t1_.ts_end(), t2_.ts_end());
      PERFETTO_DCHECK(min_end > max_start);

      auto dur = min_end - max_start;
      sqlite3_result_int64(context, static_cast<sqlite3_int64>(dur));
      break;
    }
    case Column::kPartition: {
      PERFETTO_DCHECK(t1_.partition() == t2_.partition());
      sqlite3_result_int64(context,
                           static_cast<sqlite3_int64>(t1_.partition()));
      break;
    }
    default: {
      size_t index = static_cast<size_t>(N);
      const auto& locator = table_->global_index_to_column_locator_[index];
      if (locator.defn == t1_.definition())
        t1_.ReportSqliteResult(context, locator.col_index);
      else
        t2_.ReportSqliteResult(context, locator.col_index);
      break;
    }
  }
  return SQLITE_OK;
}

SpanOperatorTable::TableDefinition::TableDefinition(
    std::string name,
    std::string partition_col,
    std::vector<Table::Column> cols)
    : name_(std::move(name)),
      partition_col_(std::move(partition_col)),
      cols_(std::move(cols)) {}

SpanOperatorTable::TableDescriptor SpanOperatorTable::TableDescriptor::Parse(
    const std::string& raw_descriptor) {
  // Descriptors have one of the following forms:
  // (1) table_name
  // (2) table_name UNPARTITIONED
  // (3) table_name PARTITIONED column_name

  base::StringSplitter splitter(raw_descriptor, ' ');
  if (!splitter.Next())
    return {};

  TableDescriptor descriptor;
  descriptor.name = splitter.cur_token();

  // Case (1): only a table name is present so just return the descriptor
  // without any partition.
  if (!splitter.Next())
    return descriptor;

  // Case (2): again we just return the descriptor without a partition.
  if (strcmp(splitter.cur_token(), "UNPARTITIONED") == 0)
    return descriptor;

  // Case (3): a partition is going to be specified. Parse the partition column
  // and return that.
  if (strcmp(splitter.cur_token(), "PARTITIONED") == 0) {
    // Partitioning was specified but no column was given. Just return the
    // empty descriptor.
    if (!splitter.Next())
      return {};

    descriptor.partition_col = splitter.cur_token();
    return descriptor;
  }

  // Any other case, return the empty descriptor.
  return {};
}

}  // namespace trace_processor
}  // namespace perfetto
