/*
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

#ifndef SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_
#define SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_

#include "src/trace_processor/storage_schema.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

class StorageTable : public Table {
 public:
  StorageTable(const TraceStorage* storage,
               std::vector<std::unique_ptr<StorageSchema::Column>> columns,
               std::vector<std::string> primary_keys);

  Table::Schema CreateSchema(int, const char* const*) override final;
  std::unique_ptr<Table::Cursor> CreateCursor(const QueryConstraints&,
                                              sqlite3_value**) override;

 protected:
  const TraceStorage* const storage_;
  StorageSchema schema_;
  std::vector<std::string> primary_keys_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STORAGE_TABLE_H_
