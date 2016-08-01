/*
Copyright (C) 2016- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include "histogram.h"
#include "itable.h"

#include "debug.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct box_count {
	int   count;
	void *data;
};

struct histogram {
	struct itable *buckets;
	double bucket_size;
};

struct histogram *histogram_create(double bucket_size) {

	if(bucket_size <= 0) {
		fatal("Bucket size should be larger than zero: %lf", bucket_size);
	}

	struct histogram *h = malloc(sizeof(struct histogram));

	h->bucket_size = bucket_size;
	h->buckets     = itable_create(0);

	return h;
}

void histogram_clear(struct histogram *h) {

	uint64_t key;
	struct box_count *box;

	itable_firstkey(h->buckets);
	while(itable_nextkey(h->buckets, &key, (void **) &box)) {
		free(box);
	}

	itable_clear(h->buckets);
}

void histogram_delete(struct histogram *h) {
	histogram_clear(h);

	if(h->buckets) {
		itable_delete(h->buckets);
	}

	free(h);
}

int histogram_size(struct histogram *h) {
	int count = 0;

	if(h->buckets) {
		count += itable_size(h->buckets);
	}

	return count;
}

double histogram_bucket_size(struct histogram *h) {
	return h->bucket_size;
}


uint64_t bucket_of(struct histogram *h, double value) {

	uint64_t b = abs(floor(value/h->bucket_size));

	/*
	 * times 2 so that we can intercalate negative and positive values. itable
	 * does not like negative keys.
	 * */
	b *= 2;

	/*
	 * odd b's are for non-negative values, even for negative.
	 * note this takes care of itable not liking the zero key, as 0 goes to 1.
	 * */

	if(value >= 0) {
		b++;
	}

	return b;
}

int histogram_insert(struct histogram *h, double value) {
	uint64_t bucket = bucket_of(h, value);

	struct box_count *box = itable_lookup(h->buckets, bucket);
	if(!box) {
		box = calloc(1, sizeof(*box));
		itable_insert(h->buckets, bucket, box);
	}

	box->count++;

	return box->count;
}

/* return the smallest value that would fall inside the bucket id */
double start_of(struct histogram *h, uint64_t b) {

	/* even b's correspond to negative values */
	int is_negative = (b % 2 == 0);

	double start;
	if(is_negative) {
		start = (b/2)*(-1*h->bucket_size);
	} else {
		start = ((b-1)/2)*h->bucket_size;
	}

	return start;
}

int histogram_count(struct histogram *h, double value) {
	uint64_t bucket = bucket_of(h, value);

	struct box_count *box = itable_lookup(h->buckets, bucket);

	if(!box) {
		return 0;
	}

	return box->count;
}

int cmp_double(const void *va, const void *vb) {
	double a = *((double *) va);
	double b = *((double *) vb);

	if(a < b) {
		return -1;
	}

	if(a > b) {
		return 1;
	}

	return 0;
}

double *histogram_buckets(struct histogram *h) {

	int n = histogram_size(h);

	if(n < 1) {
		return NULL;
	}

	double *values = calloc(histogram_size(h), sizeof(double));

	int i = 0;
	uint64_t key;
	struct box_count *box;

	itable_firstkey(h->buckets);
	while(itable_nextkey(h->buckets, &key, (void **) &box)) {
		values[i] = start_of(h, key);
		i++;
	}

	qsort(values, n, sizeof(double), cmp_double);

	return values;
}

void histogram_set_bucket(struct histogram *h, double value, int count) {
	uint64_t bucket = bucket_of(h, value);

	struct box_count *box = itable_lookup(h->buckets, bucket);
	if(!box) {
		box = calloc(1, sizeof(*box));
		itable_insert(h->buckets, bucket, box);
	}
}

void histogram_attach_data(struct histogram *h, double value, void *data) {
	uint64_t bucket = bucket_of(h, value);

	struct box_count *box = itable_lookup(h->buckets, bucket);
	if(!box) {
		box = calloc(1, sizeof(*box));
		itable_insert(h->buckets, bucket, box);
	}

	box->data = data;
}

void *histogram_get_data(struct histogram *h, double value) {
	uint64_t bucket = bucket_of(h, value);

	struct box_count *box = itable_lookup(h->buckets, bucket);
	if(!box) {
		return NULL;
	}

	return box->data;
}
