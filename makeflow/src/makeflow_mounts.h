/*
Copyright (C) 2008- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#ifndef MAKEFLOW_MOUNTS_H
#define MAKEFLOW_MOUNTS_H

#include "dag.h"

/* makeflow_mounts_parse_mountfile parses the mountfile and loads the info of each dependency into the dag structure d.
 * @param mountfile: the path of a mountfile
 * @param d: a dag structure
 * @return 0 on success, -1 on failure.
 */
int makeflow_mounts_parse_mountfile(const char *mountfile, struct dag *d);

/* makeflow_mounts_install installs all the dependencies specified in the mountfile.
 * @param d: a dag structure
 * @return 0 on success, -1 on failure.
 */

int makeflow_mounts_install(struct dag *d);

#endif

/* vim: set noexpandtab tabstop=4: */
