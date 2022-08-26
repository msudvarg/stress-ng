/*
 * Copyright (C) 2022 Marion Sudvarg
 * 
 * Enables statistics (min, max, mean, std dev)
 * to be aggregated as new values are inserted
 *
 */

#include <stdint.h>
#include <stdarg.h>

/* Aggregate statistics */
typedef struct {
    uint32_t n;
	double min;
	double max;
	double mean;
	double std;
} stress_aggregate_stats_t;

void stress_init_aggregate_stats(stress_aggregate_stats_t * stats);

void stress_aggregate_stats_insert(stress_aggregate_stats_t * stats, double val);