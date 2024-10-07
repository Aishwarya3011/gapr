#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>

extern char* X_reloc_bindtextdomain(const char* dom, const char* dir) {
	const char* rel=getenv("LIBINTL_RELOCATE");
	if(!rel || !dir)
		return libintl_bindtextdomain(dom, dir);
	fprintf(stderr, "libintl relocate: %s %s -> %s\n", dom, dir, rel);
	return libintl_bindtextdomain(dom, rel);
}
