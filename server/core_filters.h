#ifndef CORE_FILTERS_H
#define CORE_FILTERS_H

#include "httpd.h"

void process_buffer(char *buf);
int ap_read_int_from_socket(void);

#endif /* CORE_FILTERS_H */ 