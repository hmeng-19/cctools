/*
Copyright (C) 2008- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include "work_queue.h"
#include "work_queue_resources.h"

#include "list.h"

struct work_queue_file {
	int type;		// WORK_QUEUE_FILE, WORK_QUEUE_BUFFER, WORK_QUEUE_REMOTECMD, WORK_QUEUE_FILE_PIECE
	int flags;		// WORK_QUEUE_CACHE or others in the future.
	int length;		// length of payload
	off_t offset;		// file offset for WORK_QUEUE_FILE_PIECE
	off_t piece_length;	// file piece length for WORK_QUEUE_FILE_PIECE
	void *payload;		// name on master machine or buffer of data.
	char *remote_name;	// name on remote machine.
};

struct work_queue_task *work_queue_wait_internal(struct work_queue *q, int timeout, struct list *aux_links, struct list *active_aux_links);

void work_queue_get_resources( struct work_queue *q, struct work_queue_resources *r );

