/*
Copyright (C) 2008- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#ifndef MOUNTFILE_H
#define MOUNTFILE_H

#include "dag.h"

/* mountfile_parse parses the mountfile and loads the info of each dependency into the dag structure d.
 * @param mountfile: the path of a mountfile
 * @param d: a dag structure
 * @return 0 on success, -1 on failure.
 */
int mountfile_parse(const char *mountfile, struct dag *d);

/* mount_install_all installs all the dependencies specified in the mountfile.
 * @param d: a dag structure
 * @return 0 on success, -1 on failure.
 */

int mount_install_all(struct dag *d);

/* mount_uninstall_all uninstalls all the dependencies specified in the mountfile.
 * @param d: a dag structure
 * @return 0 on success, -1 on failure.
 */
int mount_uninstall_all(struct dag *d);

#endif

/* vim: set noexpandtab tabstop=4: */
