/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
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

#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/ir_optimization.h"
#include "compiler/glsl/linker_util.h"
#include "compiler/glsl/program.h"
#include "compiler/glsl/shader_cache.h"

#include "st_nir.h"
#include "st_shader_cache.h"
#include "st_program.h"

#include "main/glspirv.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"

static GLboolean
link_shader(struct gl_context *ctx, struct gl_shader_program *prog)
{
   GLboolean ret;
   struct st_context *sctx = st_context(ctx);
   struct pipe_screen *pscreen = sctx->screen;

   /* Return early if we are loading the shader from on-disk cache */
   if (st_load_nir_from_disk_cache(ctx, prog)) {
      return GL_TRUE;
   }

   MESA_TRACE_FUNC();

   assert(prog->data->LinkStatus);

   /* Skip the GLSL steps when using SPIR-V. */
   if (prog->data->spirv) {
      return st_link_nir(ctx, prog);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      struct gl_linked_shader *shader = prog->_LinkedShaders[i];
      exec_list *ir = shader->ir;
      gl_shader_stage stage = shader->Stage;
      const struct gl_shader_compiler_options *options =
            &ctx->Const.ShaderCompilerOptions[stage];

      enum pipe_shader_type ptarget = pipe_shader_type_from_mesa(stage);
      bool have_dround = pscreen->get_shader_param(pscreen, ptarget,
                                                   PIPE_SHADER_CAP_DROUND_SUPPORTED);

      if (!pscreen->get_param(pscreen, PIPE_CAP_INT64_DIVMOD))
         lower_64bit_integer_instructions(ir, DIV64 | MOD64);

      lower_packing_builtins(ir, ctx->Extensions.ARB_shading_language_packing,
                             ctx->Extensions.ARB_gpu_shader5,
                             ctx->st->has_half_float_packing);
      do_mat_op_to_vec(ir);

      if (stage == MESA_SHADER_FRAGMENT && pscreen->get_param(pscreen, PIPE_CAP_FBFETCH))
         lower_blend_equation_advanced(
            shader, ctx->Extensions.KHR_blend_equation_advanced_coherent);

      lower_instructions(ir, have_dround,
                         ctx->Extensions.ARB_gpu_shader5);

      do_vec_index_to_cond_assign(ir);
      if (options->MaxIfDepth == 0) {
         lower_discard(ir);
      }

      validate_ir_tree(ir);
   }

   ret = st_link_nir(ctx, prog);

   return ret;
}

extern "C" {

/**
 * Link a shader.
 */
static bool
st_link_glsl_to_nir(struct gl_context *ctx, struct gl_shader_program *prog)
{
   struct pipe_context *pctx = st_context(ctx)->pipe;

   MESA_TRACE_FUNC();

   GLboolean ret = link_shader(ctx, prog);
    
   if (pctx->link_shader) {
      void *driver_handles[PIPE_SHADER_TYPES];
      memset(driver_handles, 0, sizeof(driver_handles));

      for (uint32_t i = 0; i < MESA_SHADER_STAGES; ++i) {
         struct gl_linked_shader *shader = prog->_LinkedShaders[i];
         if (shader) {
            struct gl_program *p = shader->Program;
            if (p && p->variants) {
               enum pipe_shader_type type = pipe_shader_type_from_mesa(shader->Stage);
               driver_handles[type] = p->variants->driver_shader;
            }
         }
      }

      pctx->link_shader(pctx, driver_handles);
   }

   return ret;
}

/**
 * Link a GLSL shader program.  Called via glLinkProgram().
 */
void
st_link_shader(struct gl_context *ctx, struct gl_shader_program *prog)
{
   unsigned int i;
   bool spirv = false;

   MESA_TRACE_FUNC();

   _mesa_clear_shader_program_data(ctx, prog);

   prog->data = _mesa_create_shader_program_data();

   prog->data->LinkStatus = LINKING_SUCCESS;

   for (i = 0; i < prog->NumShaders; i++) {
      if (!prog->Shaders[i]->CompileStatus) {
	 linker_error(prog, "linking with uncompiled/unspecialized shader");
      }

      if (!i) {
         spirv = (prog->Shaders[i]->spirv_data != NULL);
      } else if (spirv && !prog->Shaders[i]->spirv_data) {
         /* The GL_ARB_gl_spirv spec adds a new bullet point to the list of
          * reasons LinkProgram can fail:
          *
          *    "All the shader objects attached to <program> do not have the
          *     same value for the SPIR_V_BINARY_ARB state."
          */
         linker_error(prog,
                      "not all attached shaders have the same "
                      "SPIR_V_BINARY_ARB state");
      }
   }
   prog->data->spirv = spirv;

   if (prog->data->LinkStatus) {
      if (!spirv)
         link_shaders(ctx, prog);
      else
         _mesa_spirv_link_shaders(ctx, prog);
   }

   /* If LinkStatus is LINKING_SUCCESS, then reset sampler validated to true.
    * Validation happens via the LinkShader call below. If LinkStatus is
    * LINKING_SKIPPED, then SamplersValidated will have been restored from the
    * shader cache.
    */
   if (prog->data->LinkStatus == LINKING_SUCCESS) {
      prog->SamplersValidated = GL_TRUE;
   }

   if (prog->data->LinkStatus && !st_link_glsl_to_nir(ctx, prog)) {
      prog->data->LinkStatus = LINKING_FAILURE;
   }

   if (prog->data->LinkStatus != LINKING_FAILURE)
      _mesa_create_program_resource_hash(prog);

   /* Return early if we are loading the shader from on-disk cache */
   if (prog->data->LinkStatus == LINKING_SKIPPED)
      return;

   if (ctx->_Shader->Flags & GLSL_DUMP) {
      if (!prog->data->LinkStatus) {
	 fprintf(stderr, "GLSL shader program %d failed to link\n", prog->Name);
      }

      if (prog->data->InfoLog && prog->data->InfoLog[0] != 0) {
	 fprintf(stderr, "GLSL shader program %d info log:\n", prog->Name);
         fprintf(stderr, "%s\n", prog->data->InfoLog);
      }
   }

#ifdef ENABLE_SHADER_CACHE
   if (prog->data->LinkStatus)
      shader_cache_write_program_metadata(ctx, prog);
#endif
}

} /* extern "C" */
