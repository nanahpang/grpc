/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/matcher/v3/filter_state.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */


#include "upb/reflection/def.h"
#include "envoy/type/matcher/v3/filter_state.upbdefs.h"
#include "envoy/type/matcher/v3/filter_state.upb_minitable.h"

extern _upb_DefPool_Init envoy_type_matcher_v3_address_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_string_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;

static const char descriptor[561] = {
    '\n', '(', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e',
    '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/',
    'f', 'i', 'l', 't', 'e', 'r', '_', 's', 't', 'a', 't', 'e',
    '.', 'p', 'r', 'o', 't', 'o', '\022', '\025', 'e', 'n', 'v', 'o',
    'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h',
    'e', 'r', '.', 'v', '3', '\032', '#', 'e', 'n', 'v', 'o', 'y',
    '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e',
    'r', '/', 'v', '3', '/', 'a', 'd', 'd', 'r', 'e', 's', 's',
    '.', 'p', 'r', 'o', 't', 'o', '\032', '\"', 'e', 'n', 'v', 'o',
    'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h',
    'e', 'r', '/', 'v', '3', '/', 's', 't', 'r', 'i', 'n', 'g',
    '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a',
    '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's',
    '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't',
    'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/',
    'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o',
    't', 'o', '\"', '\330', '\001', '\n', '\022', 'F', 'i', 'l', 't', 'e',
    'r', 'S', 't', 'a', 't', 'e', 'M', 'a', 't', 'c', 'h', 'e',
    'r', '\022', '\031', '\n', '\003', 'k', 'e', 'y', '\030', '\001', ' ', '\001',
    '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', '\020', '\001', 'R',
    '\003', 'k', 'e', 'y', '\022', 'I', '\n', '\014', 's', 't', 'r', 'i',
    'n', 'g', '_', 'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001',
    '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 't',
    'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.',
    'v', '3', '.', 'S', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't',
    'c', 'h', 'e', 'r', 'H', '\000', 'R', '\013', 's', 't', 'r', 'i',
    'n', 'g', 'M', 'a', 't', 'c', 'h', '\022', 'L', '\n', '\r', 'a',
    'd', 'd', 'r', 'e', 's', 's', '_', 'm', 'a', 't', 'c', 'h',
    '\030', '\003', ' ', '\001', '(', '\013', '2', '%', '.', 'e', 'n', 'v',
    'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c',
    'h', 'e', 'r', '.', 'v', '3', '.', 'A', 'd', 'd', 'r', 'e',
    's', 's', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R',
    '\014', 'a', 'd', 'd', 'r', 'e', 's', 's', 'M', 'a', 't', 'c',
    'h', 'B', '\016', '\n', '\007', 'm', 'a', 't', 'c', 'h', 'e', 'r',
    '\022', '\003', '\370', 'B', '\001', 'B', '\211', '\001', '\n', '#', 'i', 'o',
    '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.',
    'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm',
    'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 'B', '\020', 'F',
    'i', 'l', 't', 'e', 'r', 'S', 't', 'a', 't', 'e', 'P', 'r',
    'o', 't', 'o', 'P', '\001', 'Z', 'F', 'g', 'i', 't', 'h', 'u',
    'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p',
    'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't',
    'r', 'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n',
    'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't',
    'c', 'h', 'e', 'r', '/', 'v', '3', ';', 'm', 'a', 't', 'c',
    'h', 'e', 'r', 'v', '3', '\272', '\200', '\310', '\321', '\006', '\002', '\020',
    '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3',
};

static _upb_DefPool_Init *deps[5] = {
    &envoy_type_matcher_v3_address_proto_upbdefinit,
    &envoy_type_matcher_v3_string_proto_upbdefinit,
    &udpa_annotations_status_proto_upbdefinit,
    &validate_validate_proto_upbdefinit,
    NULL,
};

_upb_DefPool_Init envoy_type_matcher_v3_filter_state_proto_upbdefinit = {
    deps,
    &envoy_type_matcher_v3_filter_state_proto_upb_file_layout,
    "envoy/type/matcher/v3/filter_state.proto",
    UPB_STRINGVIEW_INIT(descriptor, sizeof(descriptor)),
};
