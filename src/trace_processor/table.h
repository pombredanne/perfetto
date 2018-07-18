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

#ifndef SRC_TRACE_PROCESSOR_TABLE_H_
#define SRC_TRACE_PROCESSOR_TABLE_H_

#include <sqlite3.h>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/trace_processor/query_constraints.h"

#if defined(LEAK_SANITIZER)
#include <sanitizer/lsan_interface.h>
#endif

namespace perfetto {
namespace trace_processor {

class Table : public sqlite3_vtab {
 public:
  // public for unique_ptr destructor calls.
  virtual ~Table();

 protected:
  using TableFactory = std::function<std::unique_ptr<Table>(const void*)>;
  using FindFunctionFn = void (**)(sqlite3_context*, int, sqlite3_value**);

  class Cursor : public sqlite3_vtab_cursor {
   public:
    virtual ~Cursor();

    // Methods to be implemented.
    virtual int Filter(const QueryConstraints& qc, sqlite3_value** argv) = 0;
    virtual int Next() = 0;
    virtual int Eof() = 0;
    virtual int Column(sqlite3_context* context, int N) = 0;

    virtual int Filter(int idxNum,
                       const char* idxStr,
                       int argc,
                       sqlite3_value** argv) final;
  };

  class RegisterArgs {
   public:
    static RegisterArgs* Create(sqlite3* db,
                                std::string create_stmt,
                                std::string table_name,
                                TableFactory factory,
                                const void* inner) {
      return new RegisterArgs(db, create_stmt, table_name, factory, inner);
    }

    sqlite3* db_;
    std::string create_stmt_;
    std::string table_name_;
    TableFactory factory_;
    const void* inner_;

   private:
    RegisterArgs(sqlite3*, std::string, std::string, TableFactory, const void*);
  };

  struct BestIndexInfo {
    bool order_by_consumed;
    uint32_t estimated_cost;
    std::vector<bool> omit;
  };

  Table();

  static void RegisterTable(RegisterArgs* args);

  // Methods to be implemented.
  virtual std::unique_ptr<Cursor> CreateCursor() = 0;
  virtual int BestIndex(const QueryConstraints& qc, BestIndexInfo* info) = 0;

  // Optional metods to implement.
  virtual int FindFunction(const char* name, FindFunctionFn fn, void** args);

  virtual int Open(sqlite3_vtab_cursor**) final;
  virtual int BestIndex(sqlite3_index_info*) final;

 private:
  using ModuleMap = std::map<std::string, sqlite3_module>;

  static ModuleMap* GetModuleMapInstance() {
    static ModuleMap* map = new ModuleMap();
#if defined(LEAK_SANITIZER)
    __lsan_ignore_object(map);
#endif
    return map;
  }
  static sqlite3_module CreateModule();

  static Table* ToTable(sqlite3_vtab* vtab) {
    return static_cast<Table*>(vtab);
  }

  static Cursor* ToCursor(sqlite3_vtab_cursor* cursor) {
    return static_cast<Cursor*>(cursor);
  }
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TABLE_H_
