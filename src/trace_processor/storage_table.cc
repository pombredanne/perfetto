#include "src/trace_processor/storage_table.h"

namespace perfetto {
namespace trace_processor {

StorageTable::StorageTable(std::vector<std::unique_ptr<Column>> columns)
    : columns_(std::move(columns)) {}

StorageTable::Cursor::Cursor(
    std::unique_ptr<RowIterator> iterator,
    std::vector<std::unique_ptr<StorageTable::Column>>* cols)
    : iterator_(std::move(iterator)), columns_(std::move(cols)) {}

int StorageTable::Cursor::Next() {
  iterator_->NextRow();
  return SQLITE_OK;
}

int StorageTable::Cursor::Eof() {
  return iterator_->IsEnd();
}

int StorageTable::Cursor::Column(sqlite3_context* context, int raw_col) {
  size_t column = static_cast<size_t>(raw_col);
  (*columns_)[column]->ReportResult(context, iterator_->Row());
  return SQLITE_OK;
}

Table::Schema StorageTable::ToTableSchema(std::vector<std::string> pkeys) {
  std::vector<Table::Column> columns;
  size_t i = 0;
  for (const auto& col : columns_)
    columns.emplace_back(i++, col->name(), col->GetType(), col->hidden());

  std::vector<size_t> primary_keys;
  for (const auto& p_key : pkeys)
    primary_keys.emplace_back(ColumnIndexFromName(p_key));
  return Table::Schema(std::move(columns), std::move(primary_keys));
}

size_t StorageTable::ColumnIndexFromName(const std::string& name) {
  auto p = [name](const std::unique_ptr<Column>& col) {
    return name == col->name();
  };
  auto it = std::find_if(columns_.begin(), columns_.end(), p);
  return static_cast<size_t>(std::distance(columns_.begin(), it));
}

StorageTable::Column::Column(std::string col_name, bool hidden)
    : col_name_(col_name), hidden_(hidden) {}
StorageTable::Column::~Column() = default;

StorageTable::TsEndColumn::TsEndColumn(std::string col_name,
                                       const std::deque<uint64_t>* ts_start,
                                       const std::deque<uint64_t>* dur)
    : Column(col_name, false), ts_start_(ts_start), dur_(dur) {}
StorageTable::TsEndColumn::~TsEndColumn() = default;

void StorageTable::TsEndColumn::ReportResult(sqlite3_context* ctx,
                                             uint32_t row) const {
  uint64_t add = (*ts_start_)[row] + (*dur_)[row];
  sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(add));
}

StorageTable::Column::Bounds StorageTable::TsEndColumn::BoundFilter(
    int,
    sqlite3_value*) const {
  Bounds bounds;
  bounds.max_idx = static_cast<uint32_t>(ts_start_->size());
  return bounds;
}

void StorageTable::TsEndColumn::Filter(int op,
                                       sqlite3_value* value,
                                       FilteredRowIndex* index) const {
  auto binary_op = sqlite_utils::GetPredicateForOp<uint64_t>(op);
  uint64_t extracted = sqlite_utils::ExtractSqliteValue<uint64_t>(value);
  index->FilterRows([this, &binary_op, extracted](uint32_t row) {
    uint64_t val = (*ts_start_)[row] + (*dur_)[row];
    return binary_op(val, extracted);
  });
}

StorageTable::Column::Comparator StorageTable::TsEndColumn::Sort(
    const QueryConstraints::OrderBy& ob) const {
  if (ob.desc) {
    return [this](uint32_t f, uint32_t s) {
      uint64_t a = (*ts_start_)[f] + (*dur_)[f];
      uint64_t b = (*ts_start_)[s] + (*dur_)[s];
      return sqlite_utils::CompareValuesDesc(a, b);
    };
  }
  return [this](uint32_t f, uint32_t s) {
    uint64_t a = (*ts_start_)[f] + (*dur_)[f];
    uint64_t b = (*ts_start_)[s] + (*dur_)[s];
    return sqlite_utils::CompareValuesAsc(a, b);
  };
}

StorageTable::IdColumn::IdColumn(std::string column_name, TableId table_id)
    : Column(std::move(column_name), false), table_id_(table_id) {}
StorageTable::IdColumn::~IdColumn() = default;

StorageTable::RowIterator::~RowIterator() = default;

}  // namespace trace_processor
}  // namespace perfetto
