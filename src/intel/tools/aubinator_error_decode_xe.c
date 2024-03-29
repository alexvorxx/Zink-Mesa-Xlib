/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "aubinator_error_decode_xe.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aubinator_error_decode_lib.h"
#include "intel/compiler/brw_isa_info.h"
#include "intel/dev/intel_device_info.h"

enum xe_vm_topic_type {
   XE_VM_TOPIC_TYPE_UNKNOWN = 0,
   XE_VM_TOPIC_TYPE_LENGTH,
   XE_VM_TOPIC_TYPE_DATA,
   XE_VM_TOPIC_TYPE_ERROR,
};

struct xe_vm_entry {
   uint64_t address;
   uint32_t length;
   const uint32_t *data;
};

struct xe_vm {
   /* TODO: entries could be appended sorted or a hash could be used to
    * optimize performance
    */
   struct xe_vm_entry *entries;
   uint32_t entries_len;

   struct xe_vm_entry hw_context;
};

static const char *
read_parameter_helper(const char *line, const char *parameter)
{
   if (!strstr(line, parameter))
      return NULL;

   while (*line != ':')
      line++;
   /* skip ':' and ' ' */
   line += 2;

   return line;
}

/* parse lines like 'batch_addr[0]: 0x0000effeffff5000 */
static bool
read_u64_hexacimal_parameter(const char *line, const char *parameter, uint64_t *value)
{
   line = read_parameter_helper(line, parameter);
   if (!line)
      return false;

   *value = (uint64_t)strtoull(line, NULL, 0);
   return true;
}

/* parse lines like 'PCI ID: 0x9a49' */
static bool
read_hexacimal_parameter(const char *line, const char *parameter, int *value)
{
   line = read_parameter_helper(line, parameter);
   if (!line)
      return false;

   *value = (int)strtol(line, NULL, 0);
   return true;
}

/* parse lines like 'rcs0 (physical), logical instance=0' */
static bool
read_xe_engine_name(const char *line, char *ring_name)
{
   int i;

   if (!strstr(line, " (physical), logical instance="))
      return false;

   i = 0;
   for (i = 0; *line != ' '; i++, line++)
      ring_name[i] = *line;

   ring_name[i] = 0;
   return true;
}

/* return type of VM topic lines like '[200000].data: x...' and points
 * value_ptr to first char of data of topic type
 */
static enum xe_vm_topic_type
read_xe_vm_line(const char *line, uint64_t *address, const char **value_ptr)
{
   enum xe_vm_topic_type type;
   char text_addr[64];
   int i;

   if (*line != '[')
      return XE_VM_TOPIC_TYPE_UNKNOWN;

   for (i = 0, line++; *line != ']'; i++, line++)
      text_addr[i] = *line;

   text_addr[i] = 0;
   *address = (uint64_t)strtoull(text_addr, NULL, 16);

   /* at this point line points to last address digit so +3 to point to type */
   line += 2;
   switch (*line) {
   case 'd':
      type = XE_VM_TOPIC_TYPE_DATA;
      break;
   case 'l':
      type = XE_VM_TOPIC_TYPE_LENGTH;
      break;
   case 'e':
      type = XE_VM_TOPIC_TYPE_ERROR;
      break;
   default:
      printf("type char: %c\n", *line);
      return XE_VM_TOPIC_TYPE_UNKNOWN;
   }

   for (; *line != ':'; line++);

   *value_ptr = line + 2;
   return type;
}

/*
 * similar to read_xe_vm_line() but it parses '[HWCTX].data: ...'
 */
static enum xe_vm_topic_type
read_xe_hw_sp_or_ctx_line(const char *line, const char **value_ptr, bool *is_hw_ctx)
{
   enum xe_vm_topic_type type;
   char text_addr[64];
   bool is_hw_sp;
   int i;

   if (*line != '\t')
      return XE_VM_TOPIC_TYPE_UNKNOWN;

   line++;
   if (*line != '[')
      return XE_VM_TOPIC_TYPE_UNKNOWN;

   for (i = 0, line++; *line != ']'; i++, line++)
      text_addr[i] = *line;

   text_addr[i] = 0;
   *is_hw_ctx = strncmp(text_addr, "HWCTX", strlen("HWCTX")) == 0;
   is_hw_sp =  strncmp(text_addr, "HWSP", strlen("HWSP")) == 0;
   if (*is_hw_ctx == false && is_hw_sp == false)
         return XE_VM_TOPIC_TYPE_UNKNOWN;

   /* at this point line points to last address digit so +3 to point to type */
   line += 2;
   switch (*line) {
   case 'd':
      type = XE_VM_TOPIC_TYPE_DATA;
      break;
   case 'l':
      type = XE_VM_TOPIC_TYPE_LENGTH;
      break;
   case 'e':
      type = XE_VM_TOPIC_TYPE_ERROR;
      break;
   default:
      printf("type char: %c\n", *line);
      return XE_VM_TOPIC_TYPE_UNKNOWN;
   }

   for (; *line != ':'; line++);

   *value_ptr = line + 2;
   return type;
}

static void xe_vm_init(struct xe_vm *xe_vm)
{
   xe_vm->entries = NULL;
   xe_vm->entries_len = 0;
   memset(&xe_vm->hw_context, 0, sizeof(xe_vm->hw_context));
}

static void xe_vm_fini(struct xe_vm *xe_vm)
{
   uint32_t i;

   for (i = 0; i < xe_vm->entries_len; i++)
      free((uint32_t *)xe_vm->entries[i].data);

   free((uint32_t *)xe_vm->hw_context.data);
   free(xe_vm->entries);
}

static void
xe_vm_entry_set(struct xe_vm_entry *entry, const uint64_t address,
                const uint32_t length, const uint32_t *data)
{
   entry->address = address;
   entry->length = length;
   entry->data = data;
}

static void
xe_vm_hw_ctx_set(struct xe_vm *xe_vm, const uint32_t length,
                 const uint32_t *data)
{
   xe_vm_entry_set(&xe_vm->hw_context, 0, length, data);
}

/*
 * xe_vm_fini() will take care to free data
 */
static bool
xe_vm_append(struct xe_vm *xe_vm, const uint64_t address, const uint32_t length, const uint32_t *data)
{
   size_t len = sizeof(*xe_vm->entries) * (xe_vm->entries_len + 1);

   xe_vm->entries = realloc(xe_vm->entries, len);
   if (!xe_vm->entries)
      return false;

   xe_vm_entry_set(&xe_vm->entries[xe_vm->entries_len], address, length, data);
   xe_vm->entries_len++;
   return true;
}

static const struct xe_vm_entry *
xe_vm_entry_get(struct xe_vm *xe_vm, const uint64_t address)
{
   uint32_t i;

   for (i = 0; i < xe_vm->entries_len; i++) {
      struct xe_vm_entry *entry = &xe_vm->entries[i];

      if (entry->address == address)
         return entry;

      if (address > entry->address &&
          address < (entry->address + entry->length))
         return entry;
   }

   return NULL;
}

static uint32_t *
xe_vm_entry_address_get_data(const struct xe_vm_entry *entry, const uint64_t address)
{
   uint32_t offset = (address - entry->address) / sizeof(uint32_t);
   return (uint32_t *)&entry->data[offset];
}

static uint32_t
xe_vm_entry_address_get_len(const struct xe_vm_entry *entry, const uint64_t address)
{
   return entry->length - (address - entry->address);
}

static bool
ascii85_decode_allocated(const char *in, uint32_t *out, uint32_t vm_entry_bytes_len)
{
   const uint32_t dword_len = vm_entry_bytes_len / sizeof(uint32_t);
   uint32_t i;

   for (i = 0; (*in >= '!') && (*in <= 'z') && (i < dword_len); i++)
      in = ascii85_decode_char(in, &out[i]);

   if (dword_len != i)
      printf("mismatch dword_len=%u i=%u\n", dword_len, i);

   return dword_len == i && (*in < '!' || *in > 'z');
}

static struct intel_batch_decode_bo
get_bo(void *user_data, bool ppgtt, uint64_t bo_addr)
{
   struct intel_batch_decode_bo ret = {};
   const struct xe_vm_entry *vm_entry;
   struct xe_vm *xe_vm = user_data;

   if (!ppgtt)
      return ret;

   vm_entry = xe_vm_entry_get(xe_vm, bo_addr);
   if (!vm_entry)
      return ret;

   ret.addr = bo_addr;
   ret.map = xe_vm_entry_address_get_data(vm_entry, bo_addr);
   ret.size = xe_vm_entry_address_get_len(vm_entry, bo_addr);

   return ret;
}

static void
print_batch(struct intel_batch_decode_ctx *batch_ctx, const uint32_t *bb_data,
            const uint64_t bb_addr, uint32_t bb_len, const char *buffer_name,
            const char *engine_name, enum intel_engine_class engine_class,
            enum intel_batch_decode_flags batch_flags,
            bool option_print_all_bb, bool ring_wraps)
{
   bool is_ring_buffer;

   printf("--- %s (%s) at 0x%016"PRIx64"\n", buffer_name, engine_name, bb_addr);

   /* TODO: checks around buffer_name are copied from i915, if Xe KMD
    * starts to dump HW context or ring buffer this might become
    * useful.
    */
   is_ring_buffer = strcmp(buffer_name, "ring buffer") == 0;
   if (option_print_all_bb || is_ring_buffer ||
       strcmp(buffer_name, "batch buffer") == 0 ||
       strcmp(buffer_name, "HW Context") == 0) {
      if (is_ring_buffer && ring_wraps)
         batch_ctx->flags &= ~INTEL_BATCH_DECODE_OFFSETS;
      batch_ctx->engine = engine_class;
      intel_print_batch(batch_ctx, bb_data, bb_len, bb_addr, is_ring_buffer);
      batch_ctx->flags = batch_flags;
      printf("\n");
   }
}

void
read_xe_data_file(FILE *file,
                  enum intel_batch_decode_flags batch_flags,
                  const char *spec_xml_path,
                  bool option_dump_kernels,
                  bool option_print_all_bb)
{
   struct intel_batch_decode_ctx batch_ctx;
   struct intel_device_info devinfo;
   struct intel_spec *spec = NULL;
   struct brw_isa_info isa;
   struct {
      uint64_t *addrs;
      uint8_t len;
   } batch_buffers = { .addrs = NULL, .len = 0 };
   enum intel_engine_class engine_class = INTEL_ENGINE_CLASS_INVALID;
   uint32_t *vm_entry_data = NULL;
   uint32_t vm_entry_len = 0;
   bool ring_wraps = false;
   uint64_t acthd = 0;
   struct xe_vm xe_vm;
   char *line = NULL;
   size_t line_size;
   enum  {
      TOPIC_DEVICE = 0,
      TOPIC_GUC_CT,
      TOPIC_JOB,
      TOPIC_HW_ENGINES,
      TOPIC_VM,
      TOPIC_INVALID,
   } xe_topic = TOPIC_INVALID;

   xe_vm_init(&xe_vm);

   while (getline(&line, &line_size, file) > 0) {
      static const char *xe_topic_strings[] = {
         "**** Xe Device Coredump ****",
         "**** GuC CT ****",
         "**** Job ****",
         "**** HW Engines ****",
         "**** VM state ****",
      };
      bool topic_changed = false;
      bool print_line = true;

      /* handle Xe dump topics */
      for (int i = 0; i < ARRAY_SIZE(xe_topic_strings); i++) {
         if (strncmp(xe_topic_strings[i], line, strlen(xe_topic_strings[i])) == 0) {
            topic_changed = true;
            xe_topic = i;
            print_line = (xe_topic != TOPIC_VM);
            break;
         }
      }
      if (topic_changed) {
         if (print_line)
            fputs(line, stdout);
         continue;
      }

      switch (xe_topic) {
      case TOPIC_DEVICE: {
         int int_value;

         if (read_hexacimal_parameter(line, "PCI ID", &int_value)) {
            if (intel_get_device_info_from_pci_id(int_value, &devinfo)) {
               printf("Detected GFX ver %i\n", devinfo.verx10);
               brw_init_isa_info(&isa, &devinfo);

               if (spec_xml_path == NULL)
                  spec = intel_spec_load(&devinfo);
               else
                  spec = intel_spec_load_from_path(&devinfo, spec_xml_path);
            } else {
               printf("Unable to identify devid: 0x%x\n", int_value);
            }
         }

         break;
      }
      case TOPIC_HW_ENGINES: {
         char engine_name[64];
         uint64_t u64_reg;

         if (read_xe_engine_name(line, engine_name))
            ring_name_to_class(engine_name, &engine_class);

         if (read_u64_hexacimal_parameter(line, "ACTHD", &u64_reg))
            acthd = u64_reg;

         /* TODO: parse other engine registers */
         break;
      }
      case TOPIC_JOB: {
         uint64_t u64_value;

         if (read_u64_hexacimal_parameter(line, "batch_addr[", &u64_value)) {
            batch_buffers.addrs = realloc(batch_buffers.addrs, sizeof(uint64_t) * (batch_buffers.len + 1));
            batch_buffers.addrs[batch_buffers.len] = u64_value;
            batch_buffers.len++;
         }

         break;
      }
      case TOPIC_GUC_CT: {
         enum xe_vm_topic_type type;
         const char *value_ptr;
         bool is_hw_ctx;

         /* TODO: what to do with HWSP? */
         type = read_xe_hw_sp_or_ctx_line(line, &value_ptr, &is_hw_ctx);
         if (type != XE_VM_TOPIC_TYPE_UNKNOWN) {
            print_line = false;

            if (!is_hw_ctx)
               break;

            switch (type) {
            case XE_VM_TOPIC_TYPE_DATA:
               if (!ascii85_decode_allocated(value_ptr, vm_entry_data, vm_entry_len))
                  printf("Failed to parse HWCTX data\n");
               break;
            case XE_VM_TOPIC_TYPE_LENGTH: {
               vm_entry_len = strtoul(value_ptr, NULL, 0);
               vm_entry_data = calloc(1, vm_entry_len);
               if (!vm_entry_data) {
                  printf("Out of memory to allocate a buffer to store content of HWCTX\n");
                  break;
               }

               if (is_hw_ctx)
                  xe_vm_hw_ctx_set(&xe_vm, vm_entry_len, vm_entry_data);
               break;
            }
            case XE_VM_TOPIC_TYPE_ERROR:
               printf("HWCTX not present in dump, content will be zeroed: %s\n", line);
               break;
            default:
               printf("Not expected line in HWCTX: %s", line);
            }
         }

         break;
      }
      case TOPIC_VM: {
         enum xe_vm_topic_type type;
         const char *value_ptr;
         uint64_t address;

         print_line = false;
         type = read_xe_vm_line(line, &address, &value_ptr);
         switch (type) {
         case XE_VM_TOPIC_TYPE_DATA: {
            if (!ascii85_decode_allocated(value_ptr, vm_entry_data, vm_entry_len))
               printf("Failed to parse VMA 0x%" PRIx64 " data\n", address);
            break;
         }
         case XE_VM_TOPIC_TYPE_LENGTH: {
            vm_entry_len = strtoul(value_ptr, NULL, 0);
            vm_entry_data = calloc(1, vm_entry_len);
            if (!vm_entry_data) {
               printf("Out of memory to allocate a buffer to store content of VMA 0x%" PRIx64 "\n", address);
               break;
            }
            if (!xe_vm_append(&xe_vm, address, vm_entry_len, vm_entry_data)) {
               printf("xe_vm_append() failed for VMA 0x%" PRIx64 "\n", address);
               break;
            }
            break;
         }
         case XE_VM_TOPIC_TYPE_ERROR:
            printf("VMA 0x%" PRIx64 " not present in dump, content will be zeroed: %s\n", address, line);
            break;
         default:
            printf("Not expected line in VM state: %s", line);
         }
         break;
      }
      default:
            break;
      }

      if (print_line)
         fputs(line, stdout);
   }

   printf("**** Batch buffers ****\n");
   intel_batch_decode_ctx_init_brw(&batch_ctx, &isa, &devinfo, stdout,
                                   batch_flags, spec_xml_path, get_bo,
                                   NULL, &xe_vm);
   batch_ctx.acthd = acthd;

   if (option_dump_kernels)
      batch_ctx.shader_binary = dump_shader_binary;

   for (int i = 0; i < batch_buffers.len; i++) {
      const uint64_t bb_addr = batch_buffers.addrs[i];
      const struct xe_vm_entry *vm_entry = xe_vm_entry_get(&xe_vm, bb_addr);
      const char *engine_name = intel_engines_class_to_string(engine_class);
      const char *buffer_name = "batch buffer";
      const uint32_t *bb_data;
      uint32_t bb_len;

      if (!vm_entry)
         continue;

      bb_data = xe_vm_entry_address_get_data(vm_entry, bb_addr);
      bb_len = xe_vm_entry_address_get_len(vm_entry, bb_addr);
      print_batch(&batch_ctx, bb_data, bb_addr, bb_len, buffer_name,
                  engine_name, engine_class, batch_flags, option_print_all_bb,
                  ring_wraps);
   }

   printf("**** HW context ****\n");
   if (xe_vm.hw_context.length) {
      const char *engine_name = intel_engines_class_to_string(engine_class);
      const char *buffer_name = "HW Context";
      const uint64_t bb_addr = 0;
      const uint32_t *bb_data;
      uint32_t bb_len;

      bb_data = xe_vm.hw_context.data;
      bb_len = xe_vm.hw_context.length;
      print_batch(&batch_ctx, bb_data, bb_addr, bb_len, buffer_name,
                  engine_name, engine_class, batch_flags, option_print_all_bb,
                  ring_wraps);
   }

   intel_batch_decode_ctx_finish(&batch_ctx);
   intel_spec_destroy(spec);
   free(batch_buffers.addrs);
   free(line);
   xe_vm_fini(&xe_vm);
}
