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

#ifndef SRC_TRACE_PROCESSOR_STORAGE_CURSOR_H_
#define SRC_TRACE_PROCESSOR_STORAGE_CURSOR_H_

#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

// A Cursor implementation which is has backing storage (i.e. can access a value
// at a row and column in constant time).
// Users can pass in a row iteration strategy and a column retriever; this class
// will use these to respond to cursor calls from SQLite.
class StorageCursor : public Table::Cursor {
 public:
  class RowIterator {
   public:
    virtual ~RowIterator();

    virtual void NextRow() = 0;
    virtual uint32_t Row() = 0;
    virtual bool IsEnd() = 0;
  };

  class ValueRetriever {
   public:
    using StringAndDestructor = std::pair<const char*, sqlite3_destructor_type>;

    virtual ~ValueRetriever();

    virtual uint32_t GetUint(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual uint64_t GetUlong(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual StringAndDestructor GetString(size_t, size_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual int64_t GetLong(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual int32_t GetInt(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual float GetFloat(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
    virtual double GetDouble(size_t, uint32_t) const {
      PERFETTO_FATAL("Unimplemented method");
    }
  };

  StorageCursor(Table::Schema,
                std::unique_ptr<RowIterator>,
                std::unique_ptr<ValueRetriever>);

  // Implementation of Table::Cursor.
  int Next() override;
  int Eof() override;
  int Column(sqlite3_context*, int N) override;

 private:
  Table::Schema schema_;
  std::unique_ptr<RowIterator> iterator_;
  std::unique_ptr<ValueRetriever> retriever_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STORAGE_CURSOR_H_
