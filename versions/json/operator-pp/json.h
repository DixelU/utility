#ifndef OPP_JSON_H
#define OPP_JSON_H

#include "semantic.h"

#include <stdio.h>

OppStatus opp_json_write_string(FILE* output, const char* value);
OppStatus opp_json_write_position(FILE* output, OppSourcePosition position);
OppStatus opp_json_write_span(
	FILE* output,
	OppSourceSpan span,
	const OppSemanticArena* arena
);

#endif
