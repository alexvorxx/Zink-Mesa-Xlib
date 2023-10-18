/* -*- c++ -*- */
/*
 * Copyright © 2009 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef GLSL_TYPES_H
#define GLSL_TYPES_H

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "shader_enums.h"
#include "c11/threads.h"
#include "util/blob.h"
#include "util/format/u_format.h"
#include "util/macros.h"

struct glsl_type;

#ifdef __cplusplus
extern "C" {
#endif

struct _mesa_glsl_parse_state;
struct glsl_symbol_table;

extern void
glsl_type_singleton_init_or_ref(void);

extern void
glsl_type_singleton_decref(void);

extern void
_mesa_glsl_initialize_types(struct _mesa_glsl_parse_state *state);

void encode_type_to_blob(struct blob *blob, const struct glsl_type *type);

const struct glsl_type *decode_type_from_blob(struct blob_reader *blob);

typedef void (*glsl_type_size_align_func)(const struct glsl_type *type,
                                          unsigned *size, unsigned *align);

enum glsl_base_type {
   /* Note: GLSL_TYPE_UINT, GLSL_TYPE_INT, and GLSL_TYPE_FLOAT must be 0, 1,
    * and 2 so that they will fit in the 2 bits of glsl_type::sampled_type.
    */
   GLSL_TYPE_UINT = 0,
   GLSL_TYPE_INT,
   GLSL_TYPE_FLOAT,
   GLSL_TYPE_FLOAT16,
   GLSL_TYPE_DOUBLE,
   GLSL_TYPE_UINT8,
   GLSL_TYPE_INT8,
   GLSL_TYPE_UINT16,
   GLSL_TYPE_INT16,
   GLSL_TYPE_UINT64,
   GLSL_TYPE_INT64,
   GLSL_TYPE_BOOL,
   GLSL_TYPE_COOPERATIVE_MATRIX,
   GLSL_TYPE_SAMPLER,
   GLSL_TYPE_TEXTURE,
   GLSL_TYPE_IMAGE,
   GLSL_TYPE_ATOMIC_UINT,
   GLSL_TYPE_STRUCT,
   GLSL_TYPE_INTERFACE,
   GLSL_TYPE_ARRAY,
   GLSL_TYPE_VOID,
   GLSL_TYPE_SUBROUTINE,
   GLSL_TYPE_ERROR
};

/* Return the bit size of a type. Note that this differs from 
 * glsl_get_bit_size in that it returns 32 bits for bools, whereas at
 * the NIR level we would want to return 1 bit for bools.
 */
static unsigned glsl_base_type_bit_size(enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_FLOAT: /* TODO handle mediump */
   case GLSL_TYPE_SUBROUTINE:
      return 32;

   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
      return 16;

   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_INT8:
      return 8;

   case GLSL_TYPE_DOUBLE:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_IMAGE:
   case GLSL_TYPE_TEXTURE:
   case GLSL_TYPE_SAMPLER:
      return 64;

   default:
      /* For GLSL_TYPE_STRUCT etc, it should be ok to return 0. This usually
       * happens when calling this method through is_64bit and is_16bit
       * methods
       */
      return 0;
   }

   return 0;
}

static inline bool glsl_base_type_is_16bit(enum glsl_base_type type)
{
   return glsl_base_type_bit_size(type) == 16;
}

static inline bool glsl_base_type_is_64bit(enum glsl_base_type type)
{
   return glsl_base_type_bit_size(type) == 64;
}

static inline bool glsl_base_type_is_integer(enum glsl_base_type type)
{
   return type == GLSL_TYPE_UINT8 ||
          type == GLSL_TYPE_INT8 ||
          type == GLSL_TYPE_UINT16 ||
          type == GLSL_TYPE_INT16 ||
          type == GLSL_TYPE_UINT ||
          type == GLSL_TYPE_INT ||
          type == GLSL_TYPE_UINT64 ||
          type == GLSL_TYPE_INT64 ||
          type == GLSL_TYPE_BOOL ||
          type == GLSL_TYPE_SAMPLER ||
          type == GLSL_TYPE_TEXTURE ||
          type == GLSL_TYPE_IMAGE;
}

static inline unsigned int
glsl_base_type_get_bit_size(const enum glsl_base_type base_type)
{
   switch (base_type) {
   case GLSL_TYPE_BOOL:
      return 1;

   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_FLOAT: /* TODO handle mediump */
   case GLSL_TYPE_SUBROUTINE:
   case GLSL_TYPE_COOPERATIVE_MATRIX:
      return 32;

   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
      return 16;

   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_INT8:
      return 8;

   case GLSL_TYPE_DOUBLE:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_IMAGE:
   case GLSL_TYPE_SAMPLER:
   case GLSL_TYPE_TEXTURE:
      return 64;

   default:
      unreachable("unknown base type");
   }

   return 0;
}

static inline enum glsl_base_type
glsl_unsigned_base_type_of(enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_INT:
      return GLSL_TYPE_UINT;
   case GLSL_TYPE_INT8:
      return GLSL_TYPE_UINT8;
   case GLSL_TYPE_INT16:
      return GLSL_TYPE_UINT16;
   case GLSL_TYPE_INT64:
      return GLSL_TYPE_UINT64;
   default:
      assert(type == GLSL_TYPE_UINT ||
             type == GLSL_TYPE_UINT8 ||
             type == GLSL_TYPE_UINT16 ||
             type == GLSL_TYPE_UINT64);
      return type;
   }
}

static inline enum glsl_base_type
glsl_signed_base_type_of(enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_UINT:
      return GLSL_TYPE_INT;
   case GLSL_TYPE_UINT8:
      return GLSL_TYPE_INT8;
   case GLSL_TYPE_UINT16:
      return GLSL_TYPE_INT16;
   case GLSL_TYPE_UINT64:
      return GLSL_TYPE_INT64;
   default:
      assert(type == GLSL_TYPE_INT ||
             type == GLSL_TYPE_INT8 ||
             type == GLSL_TYPE_INT16 ||
             type == GLSL_TYPE_INT64);
      return type;
   }
}

enum glsl_sampler_dim {
   GLSL_SAMPLER_DIM_1D = 0,
   GLSL_SAMPLER_DIM_2D,
   GLSL_SAMPLER_DIM_3D,
   GLSL_SAMPLER_DIM_CUBE,
   GLSL_SAMPLER_DIM_RECT,
   GLSL_SAMPLER_DIM_BUF,
   GLSL_SAMPLER_DIM_EXTERNAL,
   GLSL_SAMPLER_DIM_MS,
   GLSL_SAMPLER_DIM_SUBPASS, /* for vulkan input attachments */
   GLSL_SAMPLER_DIM_SUBPASS_MS, /* for multisampled vulkan input attachments */
};

int
glsl_get_sampler_dim_coordinate_components(enum glsl_sampler_dim dim);

enum glsl_matrix_layout {
   /**
    * The layout of the matrix is inherited from the object containing the
    * matrix (the top level structure or the uniform block).
    */
   GLSL_MATRIX_LAYOUT_INHERITED,

   /**
    * Explicit column-major layout
    *
    * If a uniform block doesn't have an explicit layout set, it will default
    * to this layout.
    */
   GLSL_MATRIX_LAYOUT_COLUMN_MAJOR,

   /**
    * Row-major layout
    */
   GLSL_MATRIX_LAYOUT_ROW_MAJOR
};

enum {
   GLSL_PRECISION_NONE = 0,
   GLSL_PRECISION_HIGH,
   GLSL_PRECISION_MEDIUM,
   GLSL_PRECISION_LOW
};

enum glsl_cmat_use {
   GLSL_CMAT_USE_NONE = 0,
   GLSL_CMAT_USE_A,
   GLSL_CMAT_USE_B,
   GLSL_CMAT_USE_ACCUMULATOR,
};

struct glsl_cmat_description {
   /* MSVC can't merge bitfields of different types and also sign extend enums,
    * so use uint8_t for those cases.
    */
   uint8_t element_type:5; /* enum glsl_base_type */
   uint8_t scope:3; /* mesa_scope */
   uint8_t rows;
   uint8_t cols;
   uint8_t use; /* enum glsl_cmat_use */
};

const char *glsl_get_type_name(const struct glsl_type *type);

struct glsl_type {
   uint32_t gl_type;
   enum glsl_base_type base_type:8;

   enum glsl_base_type sampled_type:8; /**< Type of data returned using this
                                        * sampler or image.  Only \c
                                        * GLSL_TYPE_FLOAT, \c GLSL_TYPE_INT,
                                        * and \c GLSL_TYPE_UINT are valid.
                                        */

   unsigned sampler_dimensionality:4; /**< \see glsl_sampler_dim */
   unsigned sampler_shadow:1;
   unsigned sampler_array:1;
   unsigned interface_packing:2;
   unsigned interface_row_major:1;

   struct glsl_cmat_description cmat_desc;

   /**
    * For \c GLSL_TYPE_STRUCT this specifies if the struct is packed or not.
    *
    * Only used for Compute kernels
    */
   unsigned packed:1;

   unsigned has_builtin_name:1;

   /**
    * \name Vector and matrix element counts
    *
    * For scalars, each of these values will be 1.  For non-numeric types
    * these will be 0.
    */
   /*@{*/
   uint8_t vector_elements;    /**< 1, 2, 3, or 4 vector elements. */
   uint8_t matrix_columns;     /**< 1, 2, 3, or 4 matrix columns. */
   /*@}*/

   /**
    * For \c GLSL_TYPE_ARRAY, this is the length of the array.  For
    * \c GLSL_TYPE_STRUCT or \c GLSL_TYPE_INTERFACE, it is the number of
    * elements in the structure and the number of values pointed to by
    * \c fields.structure (below).
    */
   unsigned length;

   /**
    * Identifier to the name of the data type
    *
    * Use glsl_get_type_name() to access the actual name.
    */
   uintptr_t name_id;

   /**
    * Explicit array, matrix, or vector stride.  This is used to communicate
    * explicit array layouts from SPIR-V.  Should be 0 if the type has no
    * explicit stride.
    */
   unsigned explicit_stride;

   /**
    * Explicit alignment. This is used to communicate explicit alignment
    * constraints. Should be 0 if the type has no explicit alignment
    * constraint.
    */
   unsigned explicit_alignment;

   /**
    * Subtype of composite data types.
    */
   union {
      const struct glsl_type *array;            /**< Type of array elements. */
      const struct glsl_struct_field *structure;      /**< List of struct fields. */
   } fields;

/* C++ parts of glsl_type. */
#ifdef __cplusplus
   /**
    * \name Pointers to various public type singletons
    */
   /*@{*/
#define BUILTIN_TYPES_CPP_DECLARATIONS
#include "builtin_types_cpp.h"
#undef BUILTIN_TYPES_CPP_DECLARATIONS
   /*@}*/

   /**
    * Convenience accessors for vector types (shorter than get_instance()).
    * @{
    */
   static const glsl_type *vec(unsigned components);
   static const glsl_type *f16vec(unsigned components);
   static const glsl_type *dvec(unsigned components);
   static const glsl_type *ivec(unsigned components);
   static const glsl_type *uvec(unsigned components);
   static const glsl_type *bvec(unsigned components);
   static const glsl_type *i64vec(unsigned components);
   static const glsl_type *u64vec(unsigned components);
   static const glsl_type *i16vec(unsigned components);
   static const glsl_type *u16vec(unsigned components);
   static const glsl_type *i8vec(unsigned components);
   static const glsl_type *u8vec(unsigned components);
   /**@}*/

   /**
    * For numeric and boolean derived types returns the basic scalar type
    *
    * If the type is a numeric or boolean scalar, vector, or matrix type,
    * this function gets the scalar type of the individual components.  For
    * all other types, including arrays of numeric or boolean types, the
    * error type is returned.
    */
   const glsl_type *get_base_type() const;

   /**
    * Get the basic scalar type which this type aggregates.
    *
    * If the type is a numeric or boolean scalar, vector, or matrix, or an
    * array of any of those, this function gets the scalar type of the
    * individual components.  For structs and arrays of structs, this function
    * returns the struct type.  For samplers and arrays of samplers, this
    * function returns the sampler type.
    */
   const glsl_type *get_scalar_type() const;

   /**
    * Gets the "bare" type without any decorations or layout information.
    */
   const glsl_type *get_bare_type() const;

   /**
    * Gets the float16 version of this type.
    */
   const glsl_type *get_float16_type() const;

   /**
    * Gets the int16 version of this type.
    */
   const glsl_type *get_int16_type() const;

   /**
    * Gets the uint16 version of this type.
    */
   const glsl_type *get_uint16_type() const;

   /**
    * Get the instance of a built-in scalar, vector, or matrix type
    */
   static const glsl_type *get_instance(unsigned base_type, unsigned rows,
                                        unsigned columns,
                                        unsigned explicit_stride = 0,
                                        bool row_major = false,
                                        unsigned explicit_alignment = 0);

   /**
    * Get the instance of a sampler type
    */
   static const glsl_type *get_sampler_instance(enum glsl_sampler_dim dim,
                                                bool shadow,
                                                bool array,
                                                glsl_base_type type);

   static const glsl_type *get_texture_instance(enum glsl_sampler_dim dim,
                                                bool array,
                                                glsl_base_type type);

   static const glsl_type *get_image_instance(enum glsl_sampler_dim dim,
                                              bool array, glsl_base_type type);

   /**
    * Get the instance of an array type
    */
   static const glsl_type *get_array_instance(const glsl_type *element,
                                              unsigned array_size,
                                              unsigned explicit_stride = 0);

   /**
    * Get the instance of a cooperative matrix type
    */
   static const glsl_type *get_cmat_instance(const struct glsl_cmat_description desc);

   /**
    * Get the instance of a record type
    */
   static const glsl_type *get_struct_instance(const glsl_struct_field *fields,
					       unsigned num_fields,
					       const char *name,
					       bool packed = false,
					       unsigned explicit_alignment = 0);

   /**
    * Get the instance of an interface block type
    */
   static const glsl_type *get_interface_instance(const glsl_struct_field *fields,
						  unsigned num_fields,
						  enum glsl_interface_packing packing,
						  bool row_major,
						  const char *block_name);

   /**
    * Get the instance of an subroutine type
    */
   static const glsl_type *get_subroutine_instance(const char *subroutine_name);

   /**
    * Get the type resulting from a multiplication of \p type_a * \p type_b
    */
   static const glsl_type *get_mul_type(const glsl_type *type_a,
                                        const glsl_type *type_b);

   /**
    * Query the total number of scalars that make up a scalar, vector or matrix
    */
   unsigned components() const;

   /**
    * Calculate the number of components slots required to hold this type
    *
    * This is used to determine how many uniform or varying locations a type
    * might occupy.
    */
   unsigned component_slots() const;

   unsigned component_slots_aligned(unsigned offset) const;

   /**
    * Calculate offset between the base location of the struct in
    * uniform storage and a struct member.
    * For the initial call, length is the index of the member to find the
    * offset for.
    */
   unsigned struct_location_offset(unsigned length) const;

   /**
    * Calculate the number of unique values from glGetUniformLocation for the
    * elements of the type.
    *
    * This is used to allocate slots in the UniformRemapTable, the amount of
    * locations may not match with actual used storage space by the driver.
    */
   unsigned uniform_locations() const;

   /**
    * Used to count the number of varyings contained in the type ignoring
    * innermost array elements.
    */
   unsigned varying_count() const;

   /**
    * Calculate the number of vec4 slots required to hold this type.
    *
    * This is the underlying recursive type_size function for
    * count_attribute_slots() (vertex inputs and varyings) but also for
    * gallium's !PIPE_CAP_PACKED_UNIFORMS case.
    */
   unsigned count_vec4_slots(bool is_gl_vertex_input, bool bindless) const;

   /**
    * Calculate the number of vec4 slots required to hold this type.
    *
    * This is the underlying recursive type_size function for
    * gallium's PIPE_CAP_PACKED_UNIFORMS case.
    */
   unsigned count_dword_slots(bool bindless) const;

   /**
    * Calculate the number of attribute slots required to hold this type
    *
    * This implements the language rules of GLSL 1.50 for counting the number
    * of slots used by a vertex attribute.  It also determines the number of
    * varying slots the type will use up in the absence of varying packing
    * (and thus, it can be used to measure the number of varying slots used by
    * the varyings that are generated by lower_packed_varyings).
    *
    * For vertex shader attributes - doubles only take one slot.
    * For inter-shader varyings - dvec3/dvec4 take two slots.
    *
    * Vulkan doesn’t make this distinction so the argument should always be
    * false.
    */
   unsigned count_attribute_slots(bool is_gl_vertex_input) const;

   /**
    * Alignment in bytes of the start of this type in a std140 uniform
    * block.
    */
   unsigned std140_base_alignment(bool row_major) const;

   /** Size in bytes of this type in a std140 uniform block.
    *
    * Note that this is not GL_UNIFORM_SIZE (which is the number of
    * elements in the array)
    */
   unsigned std140_size(bool row_major) const;

   /**
    * Gets an explicitly laid out type with the std140 layout.
    */
   const glsl_type *get_explicit_std140_type(bool row_major) const;

   /**
    * Alignment in bytes of the start of this type in a std430 shader
    * storage block.
    */
   unsigned std430_base_alignment(bool row_major) const;

   /**
    * Calculate array stride in bytes of this type in a std430 shader storage
    * block.
    */
   unsigned std430_array_stride(bool row_major) const;

   /**
    * Size in bytes of this type in a std430 shader storage block.
    *
    * Note that this is not GL_BUFFER_SIZE
    */
   unsigned std430_size(bool row_major) const;

   /**
    * Gets an explicitly laid out type with the std430 layout.
    */
   const glsl_type *get_explicit_std430_type(bool row_major) const;

   /**
    * Gets an explicitly laid out interface type.
    */
   const glsl_type *get_explicit_interface_type(bool supports_std430) const;

   /** Returns an explicitly laid out type given a type and size/align func
    *
    * The size/align func is only called for scalar and vector types and the
    * returned type is otherwise laid out in the natural way as follows:
    *
    *  - Arrays and matrices have a stride of ALIGN(elem_size, elem_align).
    *
    *  - Structure types have their elements in-order and as tightly packed as
    *    possible following the alignment required by the size/align func.
    *
    *  - All composite types (structures, matrices, and arrays) have an
    *    alignment equal to the highest alignment of any member of the composite.
    *
    * The types returned by this function are likely not suitable for most UBO
    * or SSBO layout because they do not add the extra array and substructure
    * alignment that is required by std140 and std430.
    */
   const glsl_type *get_explicit_type_for_size_align(glsl_type_size_align_func type_info,
                                                     unsigned *size, unsigned *align) const;

   const glsl_type *replace_vec3_with_vec4() const;

   /**
    * Alignment in bytes of the start of this type in OpenCL memory.
    */
   unsigned cl_alignment() const;

   /**
    * Size in bytes of this type in OpenCL memory
    */
   unsigned cl_size() const;

   /**
    * Size in bytes of this type based on its explicit data.
    *
    * When using SPIR-V shaders (ARB_gl_spirv), memory layouts are expressed
    * through explicit offset, stride and matrix layout, so the size
    * can/should be computed used those values.
    *
    * Note that the value returned by this method is only correct if such
    * values are set, so only with SPIR-V shaders. Should not be used with
    * GLSL shaders.
    */
   unsigned explicit_size(bool align_to_stride=false) const;

   /**
    * Query whether or not a type is a scalar (non-vector and non-matrix).
    */
   bool is_scalar() const;

   /**
    * Query whether or not a type is a vector
    */
   bool is_vector() const;

   /**
    * Query whether or not a type is a matrix
    */
   bool is_matrix() const;

   /**
    * Query whether or not a type is a non-array numeric type
    */
   bool is_numeric() const;

   /**
    * Query whether or not a type is an integer.
    */
   bool is_integer() const;

   /**
    * Query whether or not a type is a 16-bit integer.
    */
   bool is_integer_16() const;

   /**
    * Query whether or not a type is an 32-bit integer.
    */
   bool is_integer_32() const;

   /**
    * Query whether or not a type is a 64-bit integer.
    */
   bool is_integer_64() const;

   /**
    * Query whether or not a type is a 32-bit or 64-bit integer
    */
   bool is_integer_32_64() const;

   /**
    * Query whether or not a type is a 16-bit or 32-bit integer
    */
   bool is_integer_16_32() const;

   /**
    * Query whether or not a type is a 16-bit, 32-bit or 64-bit integer
    */
   bool is_integer_16_32_64() const;

   /**
    * Query whether or not type is an integral type, or for struct and array
    * types, contains an integral type.
    */
   bool contains_integer() const;

   /**
    * Query whether or not type is a double type, or for struct, interface and
    * array types, contains a double type.
    */
   bool contains_double() const;

   /**
    * Query whether or not type is a 64-bit type, or for struct, interface and
    * array types, contains a double type.
    */
   bool contains_64bit() const;

   /**
    * Query whether or not a type is a float type
    */
   bool is_float() const;

   /**
    * Query whether or not a type is a half-float or float type
    */
   bool is_float_16_32() const;

   /**
    * Query whether or not a type is a half-float, float or double
    */
   bool is_float_16_32_64() const;

   /**
    * Query whether or not a type is a float or double
    */
   bool is_float_32_64() const;

   bool is_int_16_32_64() const;

   bool is_uint_16_32_64() const;

   bool is_int_16_32() const;

   bool is_uint_16_32() const;

   /**
    * Query whether or not a type is a double type
    */
   bool is_double() const;

   /**
    * Query whether a 64-bit type takes two slots.
    */
   bool is_dual_slot() const;

   /**
    * Query whether or not a type is 64-bit
    */
   bool is_64bit() const;

   /**
    * Query whether or not a type is 16-bit
    */
   bool is_16bit() const;

   /**
    * Query whether or not a type is 32-bit
    */
   bool is_32bit() const;

   /**
    * Query whether or not a type is a non-array boolean type
    */
   bool is_boolean() const;

   /**
    * Query whether or not a type is a sampler
    */
   bool is_sampler() const;

   /**
    * Query whether or not a type is a texture
    */
   bool is_texture() const;

   /**
    * Query whether or not type is a sampler, or for struct, interface and
    * array types, contains a sampler.
    */
   bool contains_sampler() const;

   /**
    * Query whether or not type is an array or for struct, interface and
    * array types, contains an array.
    */
   bool contains_array() const;

   /**
    * Query whether or not type is an image, or for struct, interface and
    * array types, contains an image.
    */
   bool contains_image() const;

   /**
    * Query whether or not a type is an image
    */
   bool is_image() const;

   /**
    * Query whether or not a type is an array
    */
   bool is_array() const;

   bool is_array_of_arrays() const;

   bool is_cmat() const;

   /**
    * Query whether or not a type is a record
    */
   bool is_struct() const;

   /**
    * Query whether or not a type is an interface
    */
   bool is_interface() const;

   /**
    * Query whether or not a type is the void type singleton.
    */
   bool is_void() const;

   /**
    * Query whether or not a type is the error type singleton.
    */
   bool is_error() const;

   /**
    * Query if a type is unnamed/anonymous (named by the parser)
    */

   bool is_subroutine() const;
   bool contains_subroutine() const;
   bool is_anonymous() const;

   /**
    * Get the type stripped of any arrays
    *
    * \return
    * Pointer to the type of elements of the first non-array type for array
    * types, or pointer to itself for non-array types.
    */
   const glsl_type *without_array() const;

   /**
    * Return the total number of elements in an array including the elements
    * in arrays of arrays.
    */
   unsigned arrays_of_arrays_size() const;

   /**
    * Return bit size for this type.
    */
   unsigned bit_size() const;


   /**
    * Query whether or not a type is an atomic_uint.
    */
   bool is_atomic_uint() const;

   /**
    * Return the amount of atomic counter storage required for a type.
    */
   unsigned atomic_size() const;

   /**
    * Return whether a type contains any atomic counters.
    */
   bool contains_atomic() const;

   /**
    * Return whether a type contains any opaque types.
    */
   bool contains_opaque() const;

   /**
    * Query the full type of a matrix row
    *
    * \return
    * If the type is not a matrix, \c glsl_type::error_type is returned.
    * Otherwise a type matching the rows of the matrix is returned.
    */
   const glsl_type *row_type() const;

   /**
    * Query the full type of a matrix column
    *
    * \return
    * If the type is not a matrix, \c glsl_type::error_type is returned.
    * Otherwise a type matching the columns of the matrix is returned.
    */
   const glsl_type *column_type() const;

   /**
    * Get the type of a structure field
    *
    * \return
    * Pointer to the type of the named field.  If the type is not a structure
    * or the named field does not exist, \c glsl_type::error_type is returned.
    */
   const glsl_type *field_type(const char *name) const;

   /**
    * Get the location of a field within a record type
    */
   int field_index(const char *name) const;

   /**
    * Query the number of elements in an array type
    *
    * \return
    * The number of elements in the array for array types or -1 for non-array
    * types.  If the number of elements in the array has not yet been declared,
    * zero is returned.
    */
   int array_size() const;

   /**
    * Query whether the array size for all dimensions has been declared.
    */
   bool is_unsized_array() const;

   /**
    * Return the number of coordinate components needed for this
    * sampler or image type.
    *
    * This is based purely on the sampler's dimensionality.  For example, this
    * returns 1 for sampler1D, and 3 for sampler2DArray.
    *
    * Note that this is often different than actual coordinate type used in
    * a texturing built-in function, since those pack additional values (such
    * as the shadow comparator or projector) into the coordinate type.
    */
   int coordinate_components() const;

   /**
    * Compares whether this type matches another type without taking into
    * account the precision in structures.
    *
    * This is applied recursively so that structures containing structure
    * members can also ignore the precision.
    */
   bool compare_no_precision(const glsl_type *b) const;

   /**
    * Compare a record type against another record type.
    *
    * This is useful for matching record types declared on the same shader
    * stage as well as across different shader stages.
    * The option to not match name is needed for matching record types
    * declared across different shader stages.
    * The option to not match locations is to deal with places where the
    * same struct is defined in a block which has a location set on it.
    */
   bool record_compare(const glsl_type *b, bool match_name,
                       bool match_locations = true,
                       bool match_precision = true) const;

   /**
    * Get the type interface packing.
    */
   enum glsl_interface_packing get_interface_packing() const;

   /**
    * Get the type interface packing used internally. For shared and packing
    * layouts this is implementation defined.
    */
   enum glsl_interface_packing get_internal_ifc_packing(bool std430_supported) const;

   /**
    * Check if the type interface is row major
    */
   bool get_interface_row_major() const;

#endif /* __cplusplus */
};

#include "builtin_types.h"

struct glsl_struct_field {
   const struct glsl_type *type;
   const char *name;

   /**
    * For interface blocks, gl_varying_slot corresponding to the input/output
    * if this is a built-in input/output (i.e. a member of the built-in
    * gl_PerVertex interface block); -1 otherwise.
    *
    * Ignored for structs.
    */
   int location;

   /**
    * For interface blocks, members may explicitly assign the component used
    * by a varying. Ignored for structs.
    */
   int component;

   /**
    * For interface blocks, members may have an explicit byte offset
    * specified; -1 otherwise. Also used for xfb_offset layout qualifier.
    *
    * Unless used for xfb_offset this field is ignored for structs.
    */
   int offset;

   /**
    * For interface blocks, members may define a transform feedback buffer;
    * -1 otherwise.
    */
   int xfb_buffer;

   /**
    * For interface blocks, members may define a transform feedback stride;
    * -1 otherwise.
    */
   int xfb_stride;

   /**
    * Layout format, applicable to image variables only.
    */
   enum pipe_format image_format;

   union {
      struct {
         /**
          * For interface blocks, the interpolation mode (as in
          * ir_variable::interpolation).  0 otherwise.
          */
         unsigned interpolation:3;

         /**
          * For interface blocks, 1 if this variable uses centroid interpolation (as
          * in ir_variable::centroid).  0 otherwise.
          */
         unsigned centroid:1;

         /**
          * For interface blocks, 1 if this variable uses sample interpolation (as
          * in ir_variable::sample). 0 otherwise.
          */
         unsigned sample:1;

         /**
          * Layout of the matrix.  Uses glsl_matrix_layout values.
          */
         unsigned matrix_layout:2;

         /**
          * For interface blocks, 1 if this variable is a per-patch input or output
          * (as in ir_variable::patch). 0 otherwise.
          */
         unsigned patch:1;

         /**
          * Precision qualifier
          */
         unsigned precision:2;

         /**
          * Memory qualifiers, applicable to buffer variables defined in shader
          * storage buffer objects (SSBOs)
          */
         unsigned memory_read_only:1;
         unsigned memory_write_only:1;
         unsigned memory_coherent:1;
         unsigned memory_volatile:1;
         unsigned memory_restrict:1;

         /**
          * Any of the xfb_* qualifiers trigger the shader to be in transform
          * feedback mode so we need to keep track of whether the buffer was
          * explicitly set or if its just been assigned the default global value.
          */
         unsigned explicit_xfb_buffer:1;

         unsigned implicit_sized_array:1;
      };
      unsigned flags;
   };
#ifdef __cplusplus
#define DEFAULT_CONSTRUCTORS(_type, _name)                  \
   type(_type), name(_name), location(-1), component(-1), offset(-1), \
   xfb_buffer(0),  xfb_stride(0), image_format(PIPE_FORMAT_NONE), flags(0) \

   glsl_struct_field(const struct glsl_type *_type,
                     int _precision,
                     const char *_name)
      : DEFAULT_CONSTRUCTORS(_type, _name)
   {
      matrix_layout = GLSL_MATRIX_LAYOUT_INHERITED;
      precision = _precision;
   }

   glsl_struct_field(const struct glsl_type *_type, const char *_name)
      : DEFAULT_CONSTRUCTORS(_type, _name)
   {
      matrix_layout = GLSL_MATRIX_LAYOUT_INHERITED;
      precision = GLSL_PRECISION_NONE;
   }

   glsl_struct_field()
      : DEFAULT_CONSTRUCTORS(NULL, NULL)
   {
      matrix_layout = GLSL_MATRIX_LAYOUT_INHERITED;
      precision = GLSL_PRECISION_NONE;
   }
#undef DEFAULT_CONSTRUCTORS
#endif
};

enum glsl_base_type glsl_get_base_type(const struct glsl_type *t);

static inline unsigned
glsl_get_bit_size(const struct glsl_type *t)
{
   return glsl_base_type_get_bit_size(glsl_get_base_type(t));
}

static inline bool glsl_type_is_boolean(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_BOOL; }
static inline bool glsl_type_is_sampler(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_SAMPLER; }
static inline bool glsl_type_is_texture(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_TEXTURE; }
static inline bool glsl_type_is_image(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_IMAGE; }
static inline bool glsl_type_is_atomic_uint(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_ATOMIC_UINT; }
static inline bool glsl_type_is_struct(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_STRUCT; }
static inline bool glsl_type_is_interface(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_INTERFACE; }
static inline bool glsl_type_is_array(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_ARRAY; }
static inline bool glsl_type_is_cmat(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_COOPERATIVE_MATRIX; }
static inline bool glsl_type_is_void(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_VOID; }
static inline bool glsl_type_is_subroutine(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_SUBROUTINE; }
static inline bool glsl_type_is_error(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_ERROR; }
static inline bool glsl_type_is_double(const struct glsl_type *t) { return t->base_type == GLSL_TYPE_DOUBLE; }

static inline bool
glsl_type_is_numeric(const struct glsl_type *t)
{
   return t->base_type >= GLSL_TYPE_UINT &&
          t->base_type <= GLSL_TYPE_INT64;
}

static inline bool
glsl_type_is_integer(const struct glsl_type *t)
{
   return glsl_base_type_is_integer(t->base_type);
}

static inline bool
glsl_type_is_struct_or_ifc(const struct glsl_type *t)
{
   return glsl_type_is_struct(t) || glsl_type_is_interface(t);
}

static inline bool
glsl_type_is_packed(const struct glsl_type *t)
{
   return t->packed;
}

static inline bool
glsl_type_is_16bit(const struct glsl_type *t)
{
   return glsl_base_type_is_16bit(t->base_type);
}

static inline bool
glsl_type_is_32bit(const struct glsl_type *t)
{
   return t->base_type == GLSL_TYPE_UINT ||
          t->base_type == GLSL_TYPE_INT ||
          t->base_type == GLSL_TYPE_FLOAT;
}

static inline bool
glsl_type_is_64bit(const struct glsl_type *t)
{
   return glsl_base_type_is_64bit(t->base_type);
}

static inline bool
glsl_type_is_unsized_array(const struct glsl_type *t)
{
   return glsl_type_is_array(t) && t->length == 0;
}

static inline bool
glsl_type_is_array_of_arrays(const struct glsl_type *t)
{
   return glsl_type_is_array(t) && glsl_type_is_array(t->fields.array);
}

static inline bool
glsl_type_is_bare_sampler(const struct glsl_type *t)
{
   return glsl_type_is_sampler(t) && t->sampled_type == GLSL_TYPE_VOID;
}

bool glsl_type_is_vector(const struct glsl_type *t);
bool glsl_type_is_scalar(const struct glsl_type *t);
bool glsl_type_is_vector_or_scalar(const struct glsl_type *t);
bool glsl_type_is_matrix(const struct glsl_type *t);
bool glsl_type_is_array_or_matrix(const struct glsl_type *t);
bool glsl_type_is_dual_slot(const struct glsl_type *t);
bool glsl_type_is_leaf(const struct glsl_type *type);

static inline bool
glsl_matrix_type_is_row_major(const struct glsl_type *t)
{
   assert((glsl_type_is_matrix(t) && t->explicit_stride) || glsl_type_is_interface(t));
   return t->interface_row_major;
}

static inline bool
glsl_sampler_type_is_shadow(const struct glsl_type *t)
{
   assert(glsl_type_is_sampler(t));
   return t->sampler_shadow;
}

static inline bool
glsl_sampler_type_is_array(const struct glsl_type *t)
{
   assert(glsl_type_is_sampler(t) ||
          glsl_type_is_texture(t) ||
          glsl_type_is_image(t));
   return t->sampler_array;
}

static inline bool
glsl_struct_type_is_packed(const struct glsl_type *t)
{
   assert(glsl_type_is_struct(t));
   return t->packed;
}

const struct glsl_type *glsl_get_bare_type(const struct glsl_type *t);

unsigned glsl_get_length(const struct glsl_type *t);
unsigned glsl_get_vector_elements(const struct glsl_type *t);
unsigned glsl_get_components(const struct glsl_type *t);
unsigned glsl_get_matrix_columns(const struct glsl_type *t);

int glsl_array_size(const struct glsl_type *t);
unsigned glsl_get_aoa_size(const struct glsl_type *t);
const struct glsl_type *glsl_get_array_element(const struct glsl_type *t);
const struct glsl_type *glsl_without_array(const struct glsl_type *t);
const struct glsl_type *glsl_without_array_or_matrix(const struct glsl_type *t);
const struct glsl_type *glsl_type_wrap_in_arrays(const struct glsl_type *t,
                                                 const struct glsl_type *arrays);

const struct glsl_type *glsl_get_cmat_element(const struct glsl_type *t);
const struct glsl_cmat_description *glsl_get_cmat_description(const struct glsl_type *t);

unsigned glsl_atomic_size(const struct glsl_type *type);

bool glsl_type_contains_64bit(const struct glsl_type *t);
bool glsl_type_contains_image(const struct glsl_type *t);
bool glsl_contains_atomic(const struct glsl_type *t);
bool glsl_contains_double(const struct glsl_type *t);
bool glsl_contains_integer(const struct glsl_type *t);
bool glsl_contains_opaque(const struct glsl_type *t);

enum glsl_sampler_dim glsl_get_sampler_dim(const struct glsl_type *t);
enum glsl_base_type glsl_get_sampler_result_type(const struct glsl_type *t);
int glsl_get_sampler_coordinate_components(const struct glsl_type *t);

bool glsl_record_compare(const struct glsl_type *a, const struct glsl_type *b,
                         bool match_name, bool match_locations,
                         bool match_precision);

const struct glsl_type *glsl_get_struct_field(const struct glsl_type *t, unsigned index);
const struct glsl_struct_field *glsl_get_struct_field_data(const struct glsl_type *t, unsigned index);
unsigned glsl_get_struct_location_offset(const struct glsl_type *t, unsigned length);
int glsl_get_field_index(const struct glsl_type *t, const char *name);
int glsl_get_struct_field_offset(const struct glsl_type *t, unsigned index);
const char *glsl_get_struct_elem_name(const struct glsl_type *t, unsigned index);

static inline const struct glsl_type *glsl_void_type(void) { return &glsl_type_builtin_void; }
static inline const struct glsl_type *glsl_float_type(void) { return &glsl_type_builtin_float; }
static inline const struct glsl_type *glsl_float16_t_type(void) { return &glsl_type_builtin_float16_t; }
static inline const struct glsl_type *glsl_double_type(void) { return &glsl_type_builtin_double; }
static inline const struct glsl_type *glsl_vec4_type(void) { return &glsl_type_builtin_vec4; }
static inline const struct glsl_type *glsl_uvec4_type(void) { return &glsl_type_builtin_uvec4; }
static inline const struct glsl_type *glsl_ivec4_type(void) { return &glsl_type_builtin_ivec4; }
static inline const struct glsl_type *glsl_int_type(void) { return &glsl_type_builtin_int; }
static inline const struct glsl_type *glsl_uint_type(void) { return &glsl_type_builtin_uint; }
static inline const struct glsl_type *glsl_int64_t_type(void) { return &glsl_type_builtin_int64_t; }
static inline const struct glsl_type *glsl_uint64_t_type(void) { return &glsl_type_builtin_uint64_t; }
static inline const struct glsl_type *glsl_int16_t_type(void) { return &glsl_type_builtin_int16_t; }
static inline const struct glsl_type *glsl_uint16_t_type(void) { return &glsl_type_builtin_uint16_t; }
static inline const struct glsl_type *glsl_int8_t_type(void) { return &glsl_type_builtin_int8_t; }
static inline const struct glsl_type *glsl_uint8_t_type(void) { return &glsl_type_builtin_uint8_t; }
static inline const struct glsl_type *glsl_bool_type(void) { return &glsl_type_builtin_bool; }
static inline const struct glsl_type *glsl_atomic_uint_type(void) { return &glsl_type_builtin_atomic_uint; }

static inline const struct glsl_type *
glsl_floatN_t_type(unsigned bit_size)
{
   switch (bit_size) {
   case 16: return &glsl_type_builtin_float16_t;
   case 32: return &glsl_type_builtin_float;
   case 64: return &glsl_type_builtin_double;
   default:
      unreachable("Unsupported bit size");
   }
}

static inline const struct glsl_type *
glsl_intN_t_type(unsigned bit_size)
{
   switch (bit_size) {
   case 8:  return &glsl_type_builtin_int8_t;
   case 16: return &glsl_type_builtin_int16_t;
   case 32: return &glsl_type_builtin_int;
   case 64: return &glsl_type_builtin_int64_t;
   default:
      unreachable("Unsupported bit size");
   }
}

static inline const struct glsl_type *
glsl_uintN_t_type(unsigned bit_size)
{
   switch (bit_size) {
   case 8:  return &glsl_type_builtin_uint8_t;
   case 16: return &glsl_type_builtin_uint16_t;
   case 32: return &glsl_type_builtin_uint;
   case 64: return &glsl_type_builtin_uint64_t;
   default:
      unreachable("Unsupported bit size");
   }
}

const struct glsl_type *glsl_vec_type(unsigned components);
const struct glsl_type *glsl_f16vec_type(unsigned components);
const struct glsl_type *glsl_dvec_type(unsigned components);
const struct glsl_type *glsl_ivec_type(unsigned components);
const struct glsl_type *glsl_uvec_type(unsigned components);
const struct glsl_type *glsl_bvec_type(unsigned components);
const struct glsl_type *glsl_i64vec_type(unsigned components);
const struct glsl_type *glsl_u64vec_type(unsigned components);
const struct glsl_type *glsl_i16vec_type(unsigned components);
const struct glsl_type *glsl_u16vec_type(unsigned components);
const struct glsl_type *glsl_i8vec_type(unsigned components);
const struct glsl_type *glsl_u8vec_type(unsigned components);

const struct glsl_type *glsl_sampler_type(enum glsl_sampler_dim dim,
                                          bool shadow,
                                          bool array,
                                          enum glsl_base_type type);
const struct glsl_type *glsl_bare_sampler_type(void);
const struct glsl_type *glsl_bare_shadow_sampler_type(void);
const struct glsl_type *glsl_texture_type(enum glsl_sampler_dim dim,
                                          bool array,
                                          enum glsl_base_type type);
const struct glsl_type *glsl_image_type(enum glsl_sampler_dim dim,
                                        bool array, enum glsl_base_type type);
const struct glsl_type *glsl_array_type(const struct glsl_type *element,
                                        unsigned array_size,
                                        unsigned explicit_stride);
const struct glsl_type *glsl_cmat_type(const struct glsl_cmat_description *desc);
const struct glsl_type *glsl_struct_type(
   const struct glsl_struct_field *fields, unsigned num_fields, const char *name, bool packed);
const struct glsl_type *glsl_interface_type(const struct glsl_struct_field *fields,
                                            unsigned num_fields,
                                            enum glsl_interface_packing packing,
                                            bool row_major,
                                            const char *block_name);
const struct glsl_type *glsl_subroutine_type(const char *subroutine_name);

const struct glsl_type *glsl_get_row_type(const struct glsl_type *t);
const struct glsl_type *glsl_get_column_type(const struct glsl_type *t);
const struct glsl_type *glsl_get_explicit_type_for_size_align(const struct glsl_type *type,
                                                              glsl_type_size_align_func type_info,
                                                              unsigned *size, unsigned *align);
const struct glsl_type *glsl_type_replace_vec3_with_vec4(const struct glsl_type *type);

const struct glsl_type *glsl_float16_type(const struct glsl_type *t);
const struct glsl_type *glsl_int16_type(const struct glsl_type *t);
const struct glsl_type *glsl_uint16_type(const struct glsl_type *t);
const struct glsl_type *glsl_type_to_16bit(const struct glsl_type *old_type);

const struct glsl_type *glsl_scalar_type(enum glsl_base_type base_type);
const struct glsl_type *glsl_vector_type(enum glsl_base_type base_type, unsigned components);
const struct glsl_type *glsl_matrix_type(enum glsl_base_type base_type,
                                         unsigned rows, unsigned columns);
const struct glsl_type *glsl_explicit_matrix_type(const struct glsl_type *mat,
                                                  unsigned stride,
                                                  bool row_major);

const struct glsl_type *glsl_transposed_type(const struct glsl_type *t);
const struct glsl_type *glsl_texture_type_to_sampler(const struct glsl_type *t, bool is_shadow);
const struct glsl_type *glsl_sampler_type_to_texture(const struct glsl_type *t);
const struct glsl_type *glsl_replace_vector_type(const struct glsl_type *t, unsigned components);
const struct glsl_type *glsl_channel_type(const struct glsl_type *t);

unsigned glsl_type_get_sampler_count(const struct glsl_type *t);
unsigned glsl_type_get_texture_count(const struct glsl_type *t);
unsigned glsl_type_get_image_count(const struct glsl_type *t);

unsigned glsl_count_vec4_slots(const struct glsl_type *t, bool is_gl_vertex_input, bool is_bindless);
unsigned glsl_count_dword_slots(const struct glsl_type *t, bool is_bindless);
unsigned glsl_get_component_slots(const struct glsl_type *t);
unsigned glsl_get_component_slots_aligned(const struct glsl_type *t, unsigned offset);
unsigned glsl_varying_count(const struct glsl_type *t);
unsigned glsl_count_attribute_slots(const struct glsl_type *t, bool is_gl_vertex_input);

int glsl_get_cl_size(const struct glsl_type *t);
int glsl_get_cl_alignment(const struct glsl_type *t);
void glsl_get_cl_type_size_align(const struct glsl_type *t,
                                 unsigned *size, unsigned *align);

enum glsl_interface_packing glsl_get_internal_ifc_packing(const struct glsl_type *t, bool std430_supported);
enum glsl_interface_packing glsl_get_ifc_packing(const struct glsl_type *t);

unsigned glsl_get_std140_base_alignment(const struct glsl_type *t, bool row_major);
unsigned glsl_get_std140_size(const struct glsl_type *t, bool row_major);
unsigned glsl_get_std430_base_alignment(const struct glsl_type *t, bool row_major);
unsigned glsl_get_std430_size(const struct glsl_type *t, bool row_major);
unsigned glsl_get_explicit_size(const struct glsl_type *t, bool align_to_stride);
unsigned glsl_get_explicit_stride(const struct glsl_type *t);
unsigned glsl_get_explicit_alignment(const struct glsl_type *t);

void glsl_get_natural_size_align_bytes(const struct glsl_type *t, unsigned *size, unsigned *align);
void glsl_get_vec4_size_align_bytes(const struct glsl_type *type, unsigned *size, unsigned *align);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
#include "glsl_types_impl.h"
#endif

#endif /* GLSL_TYPES_H */
