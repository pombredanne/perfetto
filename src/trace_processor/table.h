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

namespace perfetto {
namespace trace_processor {

class Table : public sqlite3_vtab {
 protected:
  class Cursor : public sqlite3_vtab_cursor {
   public:
    virtual ~Cursor();

    // Implementation of sqlite3_vtab_cursor.
    virtual int Filter(int idxNum,
                       const char* idxStr,
                       int argc,
                       sqlite3_value** argv) = 0;
    virtual int Next() = 0;
    virtual int Eof() = 0;
    virtual int Column(sqlite3_context* context, int N) = 0;
  };

  Table();
  virtual ~Table();

  static sqlite3_module CreateModule();

 private:
  // Implementation for sqlite3_vtab.
  virtual int BestIndex(sqlite3_index_info*) = 0;
  virtual int Open(sqlite3_vtab_cursor**) = 0;

  static Table* ToTable(sqlite3_vtab* vtab) {
    return static_cast<Table*>(vtab);
  }

  static Cursor* ToCusor(sqlite3_vtab_cursor* cursor) {
    return static_cast<Cursor*>(cursor);
  }
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TABLE_H_
