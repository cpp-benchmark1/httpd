#ifndef MOD_USERTRACK_H
#define MOD_USERTRACK_H

#include "httpd.h"

void track_user_visit_sql(const char *username);
void update_user_status_sql(const char *username);
void log_identity_to_mongo(const char *user_input);
void delete_identity_from_mongo(const char *user_input);

#endif /* MOD_USERTRACK_H */ 