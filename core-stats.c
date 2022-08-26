/*
 * Copyright (C) 2022 Marion Sudvarg
 * 
 * Enables statistics (min, max, mean, std dev)
 * to be aggregated as new values are inserted
 *
 */

#include "core-stats.h"
#include <float.h>
#include <math.h>

void stress_init_aggregate_stats(stress_aggregate_stats_t * stats) {
    stats->n = 0;
    stats->min = DBL_MAX;
    stats->max = -DBL_MAX;
    stats->mean = 0;
    stats->std = 0;
    stats->total = 0;
    stats->var_running = 0;
}

void stress_aggregate_stats_insert(stress_aggregate_stats_t * stats, double val) {
    stats->max = val>stats->max?val:stats->max;
    stats->min = val<stats->min?val:stats->min;
    stats->total += val;
    
	//Running standard-deviation using Welford's method, based on:
	//https://jonisalonen.com/2013/deriving-welfords-method-for-computing-variance/
    double oldmean = stats->mean;
    stats->mean = stats->total/(stats->n+1);
    stats->var_running += (val - stats->mean)*(val - oldmean);
    stats->std = sqrt(stats->var_running/stats->n);

    stats->n++;
}