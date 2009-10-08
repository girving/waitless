// Search PATH for executable files

#ifndef __search_path_h__
#define __search_path_h__

#include "arch.h"

/*
 * Search for an executable named file in PATH and return a pointer to the
 * full path (either file or buffer).  If PATH is null the search path is
 * looked up in the environment.  If no such file is found search_path returns
 * null and sets errno appropriately.
 *
 * search_path has slightly different error semantics than execvp, but it
 * should be close enough.
 *
 * search_path intentionally calls stat (instead of real_stat) in order to
 * trigger the appropriate action logic when called within the preload stub.
 */
const char *search_path(char buffer[PATH_MAX], const char *file, const char *PATH);

#endif
