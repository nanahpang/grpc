/* This file was generated by upb_generator from the input file:
 *
 *     google/protobuf/struct.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */



#ifndef GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H__UPBDEFS_H_
#define GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H__UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init google_protobuf_struct_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *google_protobuf_Struct_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_protobuf_struct_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.protobuf.Struct");
}

UPB_INLINE const upb_MessageDef *google_protobuf_Struct_FieldsEntry_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_protobuf_struct_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.protobuf.Struct.FieldsEntry");
}

UPB_INLINE const upb_MessageDef *google_protobuf_Value_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_protobuf_struct_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.protobuf.Value");
}

UPB_INLINE const upb_MessageDef *google_protobuf_ListValue_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_protobuf_struct_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.protobuf.ListValue");
}


#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H__UPBDEFS_H_ */
