#include <stdint.h>
#include "../shell_2_0.h"
#include "input_formatting.h"
#include <stdlib.h>

typedef struct command_parts copa;

void clean_read_buf (uint8_t *buf, int32_t size)
{
	for (; size; size--, buf++)
		*buf = 0;
}

void set_buf (uint8_t *buf, uint32_t size, uint8_t c)
{
	for(; size; size--, buf++)
		*buf = c;
}

void create_part_copa (uint8_t *buf, uint32_t size, copa **first, copa **last)
{
	copa *tmp = malloc(sizeof(*tmp));
	tmp->part = malloc(size);
	set_buf(tmp->part, size--, '\0');
	tmp->next = NULL;
	for (; size >= 0; size--)
		tmp->part[size] = buf[size];
	if (*last) {
		(*last)->next = tmp;
		tmp->prev = *last;
		*last = tmp;
	} else {
		tmp->prev = NULL;
		*first = *last = tmp;
	}
}
