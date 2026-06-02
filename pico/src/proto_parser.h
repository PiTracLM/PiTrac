/* Pure text-protocol parser, no side effects: same source compiles into
 * firmware and host-side tests. */

#ifndef PITRAC_PICO_PROTO_PARSER_H
#define PITRAC_PICO_PROTO_PARSER_H

#include <stdbool.h>

#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses a mutable line in place. True on a recognised command; on failure
 * sets out->kind to CMD_INVALID (CMD_NONE on empty) and returns false. */
bool proto_parse_line(char *line, pitrac_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_PROTO_PARSER_H */
