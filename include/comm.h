#ifndef _SWAYLOGOUT_COMM_H
#define _SWAYLOGOUT_COMM_H

#include <stdbool.h>

struct swaylogout_password;

bool spawn_comm_child(void);
ssize_t read_comm_request(char **buf_ptr);
bool write_comm_reply(bool success);
// Requests the provided password to be checked. The password is always cleared
// when the function returns.
bool write_comm_request(struct swaylogout_password *pw);
bool read_comm_reply(void);
// FD to poll for password authentication replies.
int get_comm_reply_fd(void);

#endif
