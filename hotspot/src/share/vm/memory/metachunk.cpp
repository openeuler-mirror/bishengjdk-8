/*
 * Copyright (c) 2012, 2014, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "memory/allocation.hpp"
#include "memory/metachunk.hpp"
#include "utilities/align.hpp"
#include "utilities/copy.hpp"
#include "utilities/debug.hpp"

PRAGMA_FORMAT_MUTE_WARNINGS_FOR_GCC

class VirtualSpaceNode;

size_t Metachunk::object_alignment() {
  // Must align pointers and sizes to 8,
  // so that 64 bit types get correctly aligned.
  const size_t alignment = 8;

  // Make sure that the Klass alignment also agree.
  STATIC_ASSERT(alignment == (size_t)KlassAlignmentInBytes);

  return alignment;
}

size_t Metachunk::overhead() {
  return align_size_up(sizeof(Metachunk), object_alignment()) / BytesPerWord;
}

// Metachunk methods

Metachunk::Metachunk(ChunkIndex chunktype, bool is_class, size_t word_size,
                     VirtualSpaceNode* container)
    : Metabase<Metachunk>(word_size),
    _chunk_type(chunktype),
    _is_class(is_class),
    _sentinel(CHUNK_SENTINEL),
    _origin(origin_normal),
    _use_count(0),
    _top(NULL),
    _container(container)
{
  _top = initial_top();
  set_is_tagged_free(false);
#ifdef ASSERT
  mangle(uninitMetaWordVal);
  verify();
#endif
}

MetaWord* Metachunk::allocate(size_t word_size) {
  MetaWord* result = NULL;
  // If available, bump the pointer to allocate.
  if (free_word_size() >= word_size) {
    result = _top;
    _top = _top + word_size;
  }
  return result;
}

// _bottom points to the start of the chunk including the overhead.
size_t Metachunk::used_word_size() const {
  return pointer_delta(_top, bottom(), sizeof(MetaWord));
}

size_t Metachunk::free_word_size() const {
  return pointer_delta(end(), _top, sizeof(MetaWord));
}

void Metachunk::print_on(outputStream* st) const {
  st->print_cr("Metachunk:"
               " bottom " PTR_FORMAT " top " PTR_FORMAT
               " end " PTR_FORMAT " size " SIZE_FORMAT " (%s)",
               bottom(), _top, end(), word_size(),
               chunk_size_name(get_chunk_type()));
  if (Verbose) {
    st->print_cr("    used " SIZE_FORMAT " free " SIZE_FORMAT,
                 used_word_size(), free_word_size());
  }
}

#ifdef ASSERT
void Metachunk::mangle(juint word_value) {
  // Overwrite the payload of the chunk and not the links that
  // maintain list of chunks.
  HeapWord* start = (HeapWord*)initial_top();
  size_t size = word_size() - overhead();
  Copy::fill_to_words(start, size, word_value);
}

void Metachunk::verify() {
  assert(is_valid_sentinel(), err_msg("Chunk " PTR_FORMAT ": sentinel invalid", p2i(this)));
  const ChunkIndex chunk_type = get_chunk_type();
  assert(is_valid_chunktype(chunk_type), err_msg("Chunk " PTR_FORMAT ": Invalid chunk type.", p2i(this)));
  if (chunk_type != HumongousIndex) {
    assert(word_size() == get_size_for_nonhumongous_chunktype(chunk_type, is_class()),
           err_msg("Chunk " PTR_FORMAT ": wordsize " SIZE_FORMAT " does not fit chunk type %s.",
           p2i(this), word_size(), chunk_size_name(chunk_type)));
  }
  assert(is_valid_chunkorigin(get_origin()), err_msg("Chunk " PTR_FORMAT ": Invalid chunk origin.", p2i(this)));
  assert(bottom() <= _top && _top <= (MetaWord*)end(),
         err_msg("Chunk " PTR_FORMAT ": Chunk top out of chunk bounds.", p2i(this)));

  // For non-humongous chunks, starting address shall be aligned
  // to its chunk size. Humongous chunks start address is
  // aligned to specialized chunk size.
  const size_t required_alignment =
    (chunk_type != HumongousIndex ? word_size() : get_size_for_nonhumongous_chunktype(SpecializedIndex, is_class())) * sizeof(MetaWord);
  assert(is_aligned((address)this, required_alignment),
         err_msg("Chunk " PTR_FORMAT ": (size " SIZE_FORMAT ") not aligned to " SIZE_FORMAT ".",
         p2i(this), word_size() * sizeof(MetaWord), required_alignment));
}

#endif // ASSERT

// Helper, returns a descriptive name for the given index.
const char* chunk_size_name(ChunkIndex index) {
  switch (index) {
    case SpecializedIndex:
      return "specialized";
    case SmallIndex:
      return "small";
    case MediumIndex:
      return "medium";
    case HumongousIndex:
      return "humongous";
    default:
      return "Invalid index";
  }
}

/////////////// Unit tests ///////////////

#ifndef PRODUCT

class TestMetachunk {
 public:
  static void test() {
    const ChunkIndex chunk_type = MediumIndex;
    const bool is_class = false;
    const size_t word_size = get_size_for_nonhumongous_chunktype(chunk_type, is_class);
    // Allocate the chunk with correct alignment.
    void* memory = malloc(word_size * BytesPerWord * 2);
    assert(memory != NULL, "Failed to malloc 2MB");
    
    void* p_placement = align_up(memory, word_size * BytesPerWord);

    Metachunk* metachunk = ::new (p_placement) Metachunk(chunk_type, is_class, word_size, NULL);

    assert(metachunk->bottom() == (MetaWord*)metachunk, "assert");
    assert(metachunk->end() == (uintptr_t*)metachunk + metachunk->size(), "assert");

    // Check sizes
    assert(metachunk->size() == metachunk->word_size(), "assert");
    assert(metachunk->word_size() == pointer_delta(metachunk->end(), metachunk->bottom(),
        sizeof(MetaWord*)), "assert");

    // Check usage
    assert(metachunk->used_word_size() == metachunk->overhead(), "assert");
    assert(metachunk->free_word_size() == metachunk->word_size() - metachunk->used_word_size(), "assert");
    assert(metachunk->top() == metachunk->initial_top(), "assert");
    assert(metachunk->is_empty(), "assert");

    // Allocate
    size_t alloc_size = 64; // Words
    assert(is_size_aligned(alloc_size, Metachunk::object_alignment()), "assert");

    MetaWord* mem = metachunk->allocate(alloc_size);

    // Check post alloc
    assert(mem == metachunk->initial_top(), "assert");
    assert(mem + alloc_size == metachunk->top(), "assert");
    assert(metachunk->used_word_size() == metachunk->overhead() + alloc_size, "assert");
    assert(metachunk->free_word_size() == metachunk->word_size() - metachunk->used_word_size(), "assert");
    assert(!metachunk->is_empty(), "assert");

    // Clear chunk
    metachunk->reset_empty();

    // Check post clear
    assert(metachunk->used_word_size() == metachunk->overhead(), "assert");
    assert(metachunk->free_word_size() == metachunk->word_size() - metachunk->used_word_size(), "assert");
    assert(metachunk->top() == metachunk->initial_top(), "assert");
    assert(metachunk->is_empty(), "assert");

    free(memory);
  }
};

void TestMetachunk_test() {
  TestMetachunk::test();
}

#endif
