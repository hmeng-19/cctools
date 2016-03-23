/*
Copyright (C) 2008- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#ifndef MOUNTFILE_H
#define MOUNTFILE_H

/* mountfile_parse parses the mountfile and prepare the mountfile if is_install is non-zero;
 * mountfile_parse parses the mountfile and clean up the targets if is_install is zero.
 * @param mountfile: the path of a mountfile
 * @param is_install: prepare the mount targets or clean up the mount targets
 * @return 0 on success, -1 on failure.
 */
int mountfile_parse(const char *mountfile, int is_install);

#endif

/* vim: set noexpandtab tabstop=4: */
