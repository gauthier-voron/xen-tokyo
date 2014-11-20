#include <stdlib.h>

#include "xen-msr.h"


unsigned long parse_number(const char *str, char **end)
{
	int base = 10;

	if (*str == 'x')
		base = 16;
	else if (*str == 'o')
		base = 8;
	else if (*str == 'b')
		base = 2;
	else
		str--;
	str++;

	return strtol(str, end, base);
}

int parse_command(struct command *dest, const char *argv)
{
	char *str = (char *) argv;
	char *end;

	dest->fields = 0;

	if (*str == ':') {
		str++;
		dest->fields |= FIELD_PRINT;
	}

	dest->fields |= FIELD_ADDR;
	dest->addr = parse_number(str, &end);
	str = end;

	if (*str == '=') {
		str++;
		dest->fields |= FIELD_VALUE;
		dest->value = parse_number(str, &end);
		str = end;
	}

	if (*str == '@') {
		str++;
		dest->fields |= FIELD_TIME;
		dest->time = parse_number(str, &end);
		str = end;

		if (*str == '-') {
			str++;
			dest->fields |= FIELD_INTERVAL;
			dest->interval = parse_number(str, &end);
			str = end;
		}
	}

	if (*str != '\0')
		return -1;
	return 0;
}
