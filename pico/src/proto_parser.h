/* Pure text-protocol parser. No side effects: same source compiles into
 * firmware and host-side tests. */

#ifndef PITRAC_PICO_PROTO_PARSER_H
#define PITRAC_PICO_PROTO_PARSER_H

#include <stdbool.h>

#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses an in-place mutable line. Returns true on a recognised command;
 * on failure sets out->kind to CMD_INVALID (CMD_NONE on empty) and returns
 * false. */
bool proto_parse_line(char *line, pitrac_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_PROTO_PARSER_H */
