/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_TRACING_CORE_PROTO_PIMPL_MACROS_H_
#define INCLUDE_PERFETTO_TRACING_CORE_PROTO_PIMPL_MACROS_H_

#define PERFETTO_DECLARE_PROTO_PIMPL(CLASS, PROTO_CLASS) \
 private:                                                \
  CLASS(const CLASS&) = delete;                          \
  CLASS& operator=(const CLASS&) = delete;               \
  mutable PROTO_CLASS* impl_;                            \
  bool owned_;

#define PERFETTO_DEFINE_CTOR_AND_COPY_OPERATORS(CLASS, CTOR, PROTO_CLASS) \
  CLASS::CTOR() : impl_(new PROTO_CLASS()), owned_(true) {}               \
  CLASS::CTOR(PROTO_CLASS* x) : impl_(x), owned_(false) {}                \
  CLASS::CTOR(CTOR&& x) : impl_(x.impl_), owned_(x.owned_) {              \
    x.impl_ = nullptr;                                                    \
    x.owned_ = false;                                                     \
  }                                                                       \
  CLASS::~CTOR() {                                                        \
    if (owned_)                                                           \
      delete impl_;                                                       \
  }                                                                       \
  void CLASS::CopyFrom(const PROTO_CLASS& other) { impl_->CopyFrom(other); }

#define PERFETTO_DEFINE_STRING_ACCESSORS(CLASS, FIELD)                     \
  void CLASS::set_##FIELD(const std::string& x) { impl_->set_##FIELD(x); } \
  const std::string& CLASS::FIELD() const { return impl_->FIELD(); }

#define PERFETTO_DEFINE_SUBTYPE_ACCESSORS(CLASS, TYPE, FIELD)                \
  const TYPE CLASS::FIELD() const { return TYPE(impl_->mutable_##FIELD()); } \
  TYPE CLASS::mutable_##FIELD() { return TYPE(impl_->mutable_##FIELD()); }

#define PERFETTO_DEFINE_POD_ACCESSORS(CLASS, TYPE, FIELD)    \
  void CLASS::set_##FIELD(TYPE x) { impl_->set_##FIELD(x); } \
  TYPE CLASS::FIELD() const { return impl_->FIELD(); }

#define PERFETTO_DEFINE_ENUM_ACCESSORS(CLASS, TYPE, PROTO_TYPE, FIELD) \
  void CLASS::set_##FIELD(TYPE x) {                                    \
    impl_->set_##FIELD(static_cast<PROTO_TYPE>(x));                    \
  }                                                                    \
  TYPE CLASS::FIELD() const { return static_cast<TYPE>(impl_->FIELD()); }

#define PERFETTO_DEFINE_REPEATED_ACCESSORS(CLASS, TYPE, FIELD)              \
  int CLASS::FIELD##_size() const { return impl_->FIELD##_size(); }         \
  const TYPE& CLASS::FIELD(int index) const { return impl_->FIELD(index); } \
  TYPE* CLASS::add_##FIELD() { return impl_->add_##FIELD(); }               \
  void CLASS::clear_##FIELD() { impl_->clear_##FIELD(); }

#define PERFETTO_DEFINE_REPEATED_SUBTYPE_ACCESSORS(CLASS, TYPE, FIELD) \
  int CLASS::FIELD##_size() const { return impl_->FIELD##_size(); }    \
  const TYPE CLASS::FIELD(int index) const {                           \
    return TYPE(impl_->mutable_##FIELD(index));                        \
  }                                                                    \
  TYPE CLASS::add_##FIELD() { return TYPE(impl_->add_##FIELD()); }     \
  void CLASS::clear_##FIELD() { impl_->clear_##FIELD(); }

#endif  // INCLUDE_PERFETTO_TRACING_CORE_PROTO_PIMPL_MACROS_H_
