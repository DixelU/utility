#include "json.h"

OppStatus opp_json_write_string(FILE* output, const char* value)
{
	if (!output || !value || fputc('"', output) == EOF)
	{
		return OPP_STATUS_INVALID_ARGUMENT;
	}

	const unsigned char* cursor = (const unsigned char*)value;
	while (*cursor)
	{
		switch (*cursor)
		{
			case '"':
				if (fputs("\\\"", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\\':
				if (fputs("\\\\", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\b':
				if (fputs("\\b", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\f':
				if (fputs("\\f", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\n':
				if (fputs("\\n", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\r':
				if (fputs("\\r", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			case '\t':
				if (fputs("\\t", output) == EOF) return OPP_STATUS_INVALID_ARGUMENT;
				break;
			default:
				if (*cursor < 0x20)
				{
					if (fprintf(output, "\\u%04x", (unsigned int)*cursor) < 0)
					{
						return OPP_STATUS_INVALID_ARGUMENT;
					}
				}
				else if (fputc((int)*cursor, output) == EOF)
				{
					return OPP_STATUS_INVALID_ARGUMENT;
				}
				break;
		}
		cursor++;
	}

	return fputc('"', output) == EOF ?
		OPP_STATUS_INVALID_ARGUMENT : OPP_STATUS_OK;
}

OppStatus opp_json_write_position(FILE* output, OppSourcePosition position)
{
	if (!output)
	{
		return OPP_STATUS_INVALID_ARGUMENT;
	}
	return fprintf(
		output,
		"{\"line\":%u,\"column\":%u,\"offset\":%zu}",
		(unsigned int)position.line,
		(unsigned int)position.column,
		position.offset
	) < 0 ? OPP_STATUS_INVALID_ARGUMENT : OPP_STATUS_OK;
}

OppStatus opp_json_write_span(
	FILE* output,
	OppSourceSpan span,
	const OppSemanticArena* arena
)
{
	if (!output || !arena || fputs("{\"file\":", output) == EOF)
	{
		return OPP_STATUS_INVALID_ARGUMENT;
	}

	const char* path = opp_source_file_path(arena, span.file);
	if (path)
	{
		if (opp_json_write_string(output, path) != OPP_STATUS_OK)
		{
			return OPP_STATUS_INVALID_ARGUMENT;
		}
	}
	else if (fputs("null", output) == EOF)
	{
		return OPP_STATUS_INVALID_ARGUMENT;
	}

	if (fputs(",\"start\":", output) == EOF ||
	    opp_json_write_position(output, span.start) != OPP_STATUS_OK ||
	    fputs(",\"end\":", output) == EOF ||
	    opp_json_write_position(output, span.end) != OPP_STATUS_OK ||
	    fputc('}', output) == EOF)
	{
		return OPP_STATUS_INVALID_ARGUMENT;
	}
	return OPP_STATUS_OK;
}
