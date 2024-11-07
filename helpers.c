#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

char *strdup_noquotes(const char *input) {
	if (input == NULL)
		return NULL;
	char *s = strdup(input);
	char *s_copy = s;
	size_t lastpos = strlen(s) - 1;
	if (lastpos > 0) {
		if (
				(s[0] == '"'  && s[lastpos] == '"' )
			||  (s[0] == '\'' && s[lastpos] == '\'')
		) {
			s[lastpos] = '\0';
			++s;
		}
	}
	char *retptr = strdup(s);
	free(s_copy);
	return retptr;
}

int lenient_strcmp(char *a, char *b) {
	if (a == b) {
		return 0;
	} else if (!a) {
		return -1;
	} else if (!b) {
		return 1;
	} else {
		return strcmp(a, b);
	}
}
