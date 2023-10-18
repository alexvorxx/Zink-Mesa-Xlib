/*
 * Copyright © 2009 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef GLSL_TYPES_IMPL_H
#define GLSL_TYPES_IMPL_H

#ifdef __cplusplus

#define BUILTIN_TYPES_CPP_DEFINITIONS
#include "compiler/builtin_types_cpp.h"
#undef BUILTIN_TYPES_CPP_DEFINITIONS

inline bool glsl_type::is_boolean() const { return glsl_type_is_boolean(this); }
inline bool glsl_type::is_sampler() const { return glsl_type_is_sampler(this); }
inline bool glsl_type::is_texture() const { return glsl_type_is_texture(this); }
inline bool glsl_type::is_image() const { return glsl_type_is_image(this); }
inline bool glsl_type::is_array() const { return glsl_type_is_array(this); }
inline bool glsl_type::is_struct() const { return glsl_type_is_struct(this); }
inline bool glsl_type::is_interface() const { return glsl_type_is_interface(this); }
inline bool glsl_type::is_cmat() const { return glsl_type_is_cmat(this); }
inline bool glsl_type::is_void() const { return glsl_type_is_void(this); }
inline bool glsl_type::is_error() const { return glsl_type_is_error(this); }
inline bool glsl_type::is_subroutine() const { return glsl_type_is_subroutine(this); }
inline bool glsl_type::is_atomic_uint() const { return glsl_type_is_atomic_uint(this); }

inline bool glsl_type::is_scalar() const { return glsl_type_is_scalar(this); }
inline bool glsl_type::is_vector() const { return glsl_type_is_vector(this); }
inline bool glsl_type::is_matrix() const { return glsl_type_is_matrix(this); }
inline bool glsl_type::is_numeric() const { return glsl_type_is_numeric(this); }
inline bool glsl_type::is_integer() const { return glsl_type_is_integer(this); }
inline bool glsl_type::is_double() const { return glsl_type_is_double(this); }

inline bool glsl_type::is_array_of_arrays() const { return glsl_type_is_array_of_arrays(this); }
inline bool glsl_type::is_dual_slot() const { return glsl_type_is_dual_slot(this); }
inline bool glsl_type::is_16bit() const { return glsl_type_is_16bit(this); }
inline bool glsl_type::is_32bit() const { return glsl_type_is_32bit(this); }
inline bool glsl_type::is_64bit() const { return glsl_type_is_64bit(this); }

inline unsigned
glsl_type::components() const
{
   return vector_elements * matrix_columns;
}

inline unsigned
glsl_type::count_attribute_slots(bool is_gl_vertex_input) const
{
   return count_vec4_slots(is_gl_vertex_input, true);
}

inline const glsl_type *glsl_type::vec(unsigned components) { return glsl_vec_type(components); }
inline const glsl_type *glsl_type::f16vec(unsigned components) { return glsl_f16vec_type(components); }
inline const glsl_type *glsl_type::dvec(unsigned components) { return glsl_dvec_type(components); }
inline const glsl_type *glsl_type::ivec(unsigned components) { return glsl_ivec_type(components); }
inline const glsl_type *glsl_type::uvec(unsigned components) { return glsl_uvec_type(components); }
inline const glsl_type *glsl_type::bvec(unsigned components) { return glsl_bvec_type(components); }
inline const glsl_type *glsl_type::i64vec(unsigned components) { return glsl_i64vec_type(components); }
inline const glsl_type *glsl_type::u64vec(unsigned components) { return glsl_u64vec_type(components); }
inline const glsl_type *glsl_type::i16vec(unsigned components) { return glsl_i16vec_type(components); }
inline const glsl_type *glsl_type::u16vec(unsigned components) { return glsl_u16vec_type(components); }
inline const glsl_type *glsl_type::i8vec(unsigned components) { return glsl_i8vec_type(components); }
inline const glsl_type *glsl_type::u8vec(unsigned components) { return glsl_u8vec_type(components); }

inline bool
glsl_type::is_integer_16() const
{
   return base_type == GLSL_TYPE_UINT16 || base_type == GLSL_TYPE_INT16;
}

inline bool
glsl_type::is_integer_32() const
{
   return (base_type == GLSL_TYPE_UINT) || (base_type == GLSL_TYPE_INT);
}

inline bool
glsl_type::is_integer_64() const
{
   return base_type == GLSL_TYPE_UINT64 || base_type == GLSL_TYPE_INT64;
}

inline bool
glsl_type::is_integer_32_64() const
{
   return is_integer_32() || is_integer_64();
}

inline bool
glsl_type::is_integer_16_32() const
{
   return is_integer_16() || is_integer_32();
}

inline bool
glsl_type::is_integer_16_32_64() const
{
   return is_integer_16() || is_integer_32() || is_integer_64();
}

inline bool
glsl_type::is_float() const
{
   return base_type == GLSL_TYPE_FLOAT;
}

inline bool
glsl_type::is_float_16_32() const
{
   return base_type == GLSL_TYPE_FLOAT16 || is_float();
}

inline bool
glsl_type::is_float_16_32_64() const
{
   return base_type == GLSL_TYPE_FLOAT16 || is_float() || is_double();
}

inline bool
glsl_type::is_float_32_64() const
{
   return is_float() || is_double();
}

inline bool
glsl_type::is_int_16_32_64() const
{
   return base_type == GLSL_TYPE_INT16 ||
          base_type == GLSL_TYPE_INT ||
          base_type == GLSL_TYPE_INT64;
}

inline bool
glsl_type::is_uint_16_32_64() const
{
   return base_type == GLSL_TYPE_UINT16 ||
          base_type == GLSL_TYPE_UINT ||
          base_type == GLSL_TYPE_UINT64;
}

inline bool
glsl_type::is_int_16_32() const
{
   return base_type == GLSL_TYPE_INT ||
          base_type == GLSL_TYPE_INT16;
}

inline bool
glsl_type::is_uint_16_32() const
{
   return base_type == GLSL_TYPE_UINT ||
          base_type == GLSL_TYPE_UINT16;
}

inline bool
glsl_type::is_anonymous() const
{
   return !strncmp(glsl_get_type_name(this), "#anon", 5);
}

inline const glsl_type *
glsl_type::without_array() const
{
   const glsl_type *t = this;

   while (t->is_array())
      t = t->fields.array;

   return t;
}

inline unsigned
glsl_type::arrays_of_arrays_size() const
{
   if (!is_array())
      return 0;

   unsigned size = length;
   const glsl_type *array_base_type = fields.array;

   while (array_base_type->is_array()) {
      size = size * array_base_type->length;
      array_base_type = array_base_type->fields.array;
   }
   return size;
}

inline unsigned
glsl_type::bit_size() const
{
   return glsl_base_type_bit_size(this->base_type);
}

inline unsigned
glsl_type::atomic_size() const
{
   if (is_atomic_uint())
      return 4; /* ATOMIC_COUNTER_SIZE */
   else if (is_array())
      return length * fields.array->atomic_size();
   else
      return 0;
}

inline bool
glsl_type::contains_atomic() const
{
   return atomic_size() > 0;
}

inline const glsl_type *
glsl_type::row_type() const
{
   if (!is_matrix())
      return error_type;

   if (explicit_stride && !interface_row_major)
      return get_instance(base_type, matrix_columns, 1, explicit_stride);
   else
      return get_instance(base_type, matrix_columns, 1);
}

inline const glsl_type *
glsl_type::column_type() const
{
   if (!is_matrix())
      return error_type;

   if (interface_row_major) {
      /* If we're row-major, the vector element stride is the same as the
       * matrix stride and we have no alignment (i.e. component-aligned).
       */
      return get_instance(base_type, vector_elements, 1,
                          explicit_stride, false, 0);
   } else {
      /* Otherwise, the vector is tightly packed (stride=0).  For
       * alignment, we treat a matrix as an array of columns make the same
       * assumption that the alignment of the column is the same as the
       * alignment of the whole matrix.
       */
      return get_instance(base_type, vector_elements, 1,
                          0, false, explicit_alignment);
   }
}

inline int
glsl_type::array_size() const
{
   return is_array() ? length : -1;
}

inline bool glsl_type::is_unsized_array() const { return glsl_type_is_unsized_array(this); }

inline enum glsl_interface_packing
glsl_type::get_interface_packing() const
{
   return (enum glsl_interface_packing)interface_packing;
}

inline enum glsl_interface_packing
glsl_type::get_internal_ifc_packing(bool std430_supported) const
{
   enum glsl_interface_packing packing = this->get_interface_packing();
   if (packing == GLSL_INTERFACE_PACKING_STD140 ||
       (!std430_supported &&
        (packing == GLSL_INTERFACE_PACKING_SHARED ||
         packing == GLSL_INTERFACE_PACKING_PACKED))) {
      return GLSL_INTERFACE_PACKING_STD140;
   } else {
      assert(packing == GLSL_INTERFACE_PACKING_STD430 ||
             (std430_supported &&
              (packing == GLSL_INTERFACE_PACKING_SHARED ||
               packing == GLSL_INTERFACE_PACKING_PACKED)));
      return GLSL_INTERFACE_PACKING_STD430;
   }
}

inline bool
glsl_type::get_interface_row_major() const
{
   return (bool) interface_row_major;
}

#endif /* __cplusplus */

#endif /* GLSL_TYPES_H */
