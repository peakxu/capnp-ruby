#include "ruby_capn_proto.h"
#include "message_reader.h"
#include "packed_message_reader.h"
#include "struct_schema.h"
#include "dynamic_struct_reader.h"
#include "class_builder.h"
#include "exception.h"
#include <kj/std/iostream.h>
#include <kj/io.h>
#include <iostream>
#include <sstream>
#include <assert.h>

namespace ruby_capn_proto {
  using WrappedType = capnp::PackedMessageReader;
  VALUE PackedMessageReader::Class;

  class BufferedStringStream: public kj::BufferedInputStream {
  public:
    BufferedStringStream(std::string& s)
        : data(s), preferredReadSize(kj::maxValue), readPos(0) {}
    explicit BufferedStringStream(std::string& s, size_t preferredReadSize)
        : data(s), preferredReadSize(preferredReadSize), readPos(0) {}
    ~BufferedStringStream() {}

    const std::string& getData() { return data; }
    void resetRead(size_t preferredReadSize = kj::maxValue) {
      readPos = 0;
      this->preferredReadSize = preferredReadSize;
    }

    bool allRead() {
      return readPos == data.size();
    }

    size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      size_t amount = kj::min(maxBytes, kj::max(minBytes, preferredReadSize));
      memcpy(buffer, data.data() + readPos, amount);
      readPos += amount;
      return amount;
    }

    void skip(size_t bytes) override {
      readPos += bytes;
    }

    kj::ArrayPtr<const kj::byte> tryGetReadBuffer() override {
      size_t amount = kj::min(data.size() - readPos, preferredReadSize);
      return kj::arrayPtr(reinterpret_cast<const kj::byte*>(data.data() + readPos), amount);
    }

  private:
    size_t preferredReadSize;
    std::string data;
    std::string::size_type readPos;
  };

  void PackedMessageReader::Init() {
    ClassBuilder("PackedMessageReader", MessageReader::Class).
      defineAlloc(&alloc).
      defineMethod("initialize", &initialize).
      defineMethod("get_root", &get_root).
      store(&Class);
  }

  VALUE PackedMessageReader::alloc(VALUE klass) {
    return Data_Wrap_Struct(klass, NULL, free, ruby_xmalloc(sizeof(WrappedType)));
  }

  VALUE PackedMessageReader::initialize(VALUE self, VALUE rb_buff) {
    rb_iv_set(self, "buff", rb_buff);
    VALUE entry = rb_ary_entry(rb_buff, 0);
    char * c_str = StringValueCStr(entry);
    std::string s = c_str;
    auto buffer = BufferedStringStream(s);

    try {
      WrappedType* p = unwrap(self);
      new (p) WrappedType(buffer);
    } catch (kj::Exception ex) {
      return Exception::raise(ex);
    }

    return Qnil;
  }

  void PackedMessageReader::free(WrappedType* p) {
    p->~PackedMessageReader();
    ruby_xfree(p);
  }

  WrappedType* PackedMessageReader::unwrap(VALUE self) {
    WrappedType* p;
    Data_Get_Struct(self, WrappedType, p);
    return p;
  }

  VALUE PackedMessageReader::get_root(VALUE self, VALUE rb_schema) {
    if (rb_respond_to(rb_schema, rb_intern("schema"))) {
      rb_schema = rb_funcall(rb_schema, rb_intern("schema"), 0);
    }

    auto schema = *StructSchema::unwrap(rb_schema);
    auto reader = unwrap(self)->getRoot<capnp::DynamicStruct>(schema);
    return DynamicStructReader::create(reader, self);
  }
}
